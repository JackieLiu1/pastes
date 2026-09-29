#include "sync/webdavsync.h"
#include <QSettings>
#include <QDir>

WebDavSync::WebDavSync(HistoryService &history, SecretStore &secrets, const QString &directory, QObject *parent)
	: SyncService(parent), m_history(history), m_secrets(secrets), m_directory(directory), m_worker(new WebDavWorker)
{
	qRegisterMetaType<SyncDelivery>();
	m_worker->moveToThread(&m_thread);
	connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread.start();
	QSettings preferences;
	m_settings.url = preferences.value("sync/url").toUrl();
	m_settings.username = preferences.value("sync/username").toString();
	m_settings.enabled = preferences.value("sync/enabled", false).toBool();
	m_lastSuccess = preferences.value("sync/lastSuccess").toDateTime();
	m_status = tr("Sync is off.");
	m_timer.setSingleShot(true);
	connect(&m_timer, &QTimer::timeout, this, &WebDavSync::synchronize);
	connect(&history, &HistoryService::loaded, this, [this] {
		m_ready = true;
		if (m_settings.url.isEmpty()) return;
		QString error;
		if (!validate(&m_settings, &error)) { m_status = error; emit statusChanged(); return; }
		if (m_settings.enabled) {
			m_password = m_secrets.read(account(m_settings), &error);
			if (!error.isEmpty()) { m_status = error; emit statusChanged(); return; }
			m_credentialsLoaded = true;
		}
		// Keep deletion markers durable even while network sync is switched off.
		configure();
		if (m_settings.enabled) { bootstrap(); m_timer.start(2000); }
	});
	connect(&history, &HistoryService::entryAdded, this, [this](HistoryEntry entry, int, HistoryChange change) {
		if (m_settings.enabled && (change == HistoryChange::Captured || change == HistoryChange::Restored)) {
			capture(entry); schedule();
		}
	});
	connect(&history, &HistoryService::entryChanged, this, [this](EntryId id) {
		if (m_settings.enabled && !m_applying) { capture(m_history.find(id)); schedule(); }
	});
	connect(&history, &HistoryService::entryErasing, this, [this](HistoryEntry entry, HistoryChange change) {
		if (m_configured && change == HistoryChange::Deleted) {
			capture(entry, false, true); schedule();
		}
	});
}
WebDavSync::~WebDavSync()
{
	m_timer.stop();
	QMetaObject::invokeMethod(m_worker, &WebDavWorker::stop, Qt::BlockingQueuedConnection);
	m_thread.quit(); m_thread.wait();
}
QString WebDavSync::account(const SyncSettings &settings) const
{
	return SyncContent::digest(settings.url.toEncoded()+QByteArray(1, '\0')+settings.username.toUtf8());
}
bool WebDavSync::validate(SyncSettings *settings, QString *error)
{
	QUrl &url = settings->url;
	const bool local = url.host() == "127.0.0.1" || url.host() == "::1" || url.host() == "localhost";
	if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
		(url.scheme() != "https" && !(url.scheme() == "http" && local))) {
		*error = tr("Enter an HTTPS WebDAV folder address without embedded credentials or query parameters."); return false;
	}
	if (settings->username.contains(':') || settings->username.contains('\n') || settings->username.contains('\r')) {
		*error = tr("The account name contains unsupported characters."); return false;
	}
	url = url.adjusted(QUrl::NormalizePathSegments);
	if (!url.path().endsWith('/')) url.setPath(url.path()+"/");
	return true;
}
bool WebDavSync::save(const SyncSettings &input, const QString &password, QString *error)
{
	SyncSettings settings = input;
	if (!validate(&settings, error)) return false;
	const bool unchanged = settings.url == m_settings.url && settings.username == m_settings.username;
	if (unchanged && password.isEmpty() && settings.enabled == m_settings.enabled && m_configured) return true;
	QString secret = password;
	if (secret.isEmpty()) {
		secret = unchanged && m_credentialsLoaded ? m_password : m_secrets.read(account(settings), error);
		if (!error->isEmpty()) return false;
	} else if (!m_secrets.write(account(settings), secret, error)) return false;
	QSettings preferences;
	preferences.setValue("sync/url", settings.url);
	preferences.setValue("sync/username", settings.username);
	preferences.setValue("sync/enabled", settings.enabled);
	const bool sameAccount = account(settings) == account(m_settings);
	if (!sameAccount) preferences.remove("sync/lastSuccess");
	preferences.sync();
	if (preferences.status() != QSettings::NoError) { *error = tr("Could not save sync settings."); return false; }
	m_settings = settings; m_password = secret; m_credentialsLoaded = true;
	if (!sameAccount) m_lastSuccess = {};
	configure();
	if (m_settings.enabled && m_ready) { bootstrap(); m_timer.start(2000); }
	m_status = m_settings.enabled ? tr("Ready to sync.") : tr("Sync is off.");
	emit statusChanged();
	return true;
}
void WebDavSync::configure(void)
{
	++m_epoch;
	m_timer.stop(); m_busy = false; m_again = false;
	QMetaObject::invokeMethod(m_worker, &WebDavWorker::stop, Qt::BlockingQueuedConnection);
	m_worker->disconnect(this);
	m_applied.clear(); m_warning.clear();
	const quint64 epoch = m_epoch;
	connect(m_worker, &WebDavWorker::warning, this, [this,epoch](const QString &message) {
		if (epoch != m_epoch) return;
		m_warning = message; m_status = message; emit statusChanged();
	});
	connect(m_worker, &WebDavWorker::delivered, this, [this,epoch](const SyncDelivery &delivery) {
		if (epoch != m_epoch || !m_settings.enabled || m_applied.contains(delivery.id)) return;
		// A local command queued after this worker snapshot takes precedence.
		// The next incremental pass will reconcile it with the remote head.
		if (delivery.serial < m_serial) { m_again = true; return; }
		m_applying = true;
		m_history.mergeSynced(delivery.deleted ? HistoryEntry() : SyncContent::materialize(delivery.content), delivery.replaced);
		m_applying = false;
		m_applied.insert(delivery.id);
		QMetaObject::invokeMethod(m_worker, [worker=m_worker,id=delivery.id] { worker->acknowledge(id); });
	});
	connect(m_worker, &WebDavWorker::finished, this, [this,epoch](const QString &error, bool success, bool probe) {
		if (epoch != m_epoch) return;
		m_busy = false;
		if (!success) m_status = error;
		else if (probe) m_status = tr("Connection verified: read, write and delete are available.");
		else {
			m_lastSuccess = QDateTime::currentDateTime();
			QSettings().setValue("sync/lastSuccess", m_lastSuccess);
			m_status = m_warning.isEmpty() ? tr("Up to date.") : tr("Sync finished with skipped items: %1").arg(m_warning);
		}
		emit statusChanged();
		if (m_settings.enabled) m_timer.start(m_again ? 2000 : (success ? 30000 : 60000));
		m_again = false;
	});
	const auto settings = m_settings;
	const QString password = m_password;
	const QString directory = m_directory+"/"+account(settings);
	QMetaObject::invokeMethod(m_worker, [worker=m_worker,settings,password,directory] {
		worker->configure(settings, password, directory);
	}, Qt::QueuedConnection);
	m_configured = true;
}
void WebDavSync::bootstrap(void)
{
	for (const auto &entry : m_history.entries()) capture(entry, true);
}
void WebDavSync::capture(const HistoryEntry &entry, bool bootstrap, bool deleted)
{
	if (!m_configured || !entry || !entry->mimeData) return;
	const auto value = SyncContent::snapshot(*entry);
	if (value.kind.isEmpty()) return;
	const quint64 serial = ++m_serial;
	QMetaObject::invokeMethod(m_worker, [worker=m_worker,value,bootstrap,deleted,serial] {
		worker->capture(value, bootstrap, deleted, serial);
	}, Qt::QueuedConnection);
}
void WebDavSync::schedule(void)
{
	if (!m_settings.enabled) return;
	if (m_busy) m_again = true;
	else m_timer.start(1500);
}
void WebDavSync::run(bool probe)
{
	if (m_busy) { m_again = true; return; }
	QString error;
	if (!validate(&m_settings, &error)) { m_status = error; emit statusChanged(); return; }
	if (!probe && (!m_settings.enabled || !m_ready)) {
		m_status = tr("Enable sync to share your clipboard history."); emit statusChanged(); return;
	}
	if (!m_configured || !m_credentialsLoaded) {
		m_password = m_secrets.read(account(m_settings), &error);
		if (!error.isEmpty()) { m_status = error; emit statusChanged(); return; }
		m_credentialsLoaded = true;
		configure();
		if (m_settings.enabled && m_ready) bootstrap();
	}
	m_timer.stop(); m_busy = true;
	m_status = probe ? tr("Testing connection…") : tr("Syncing items…");
	emit statusChanged();
	QMetaObject::invokeMethod(m_worker, [worker=m_worker,probe] { worker->run(probe); }, Qt::QueuedConnection);
}
void WebDavSync::synchronize(void) { run(false); }
void WebDavSync::testConnection(void) { run(true); }
