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
	connect(&history, &HistoryService::syncWarning, this, [this](const QString &message) {
		m_warning = message;
		if (m_settings.enabled) {
			m_status = message;
			emit statusChanged();
		}
	});
	connect(&history, &HistoryService::persistenceStateChanged, this, [this] {
		if (m_busy) {
			m_again = true;
			finishRun();
		}
	});
	connect(&history, &HistoryService::durableChanged, this,
		[this](const QByteArray &) { schedule(); });
	connect(&history, &HistoryService::appliedLoaded, this,
		[this](const QString &accountId, quint64 token, const QStringList &ids) {
			if (!m_busy || token != m_runToken || accountId != account(m_settings)) return;
			for (const auto &id : ids)
				m_applied.insert(id);
		});
	connect(&history, &HistoryService::outboxLoaded, this,
		[this](const QString &accountId, quint64 token,
			const QList<SyncContent::JournalRecord> &documents) {
			if (!m_busy || token != m_runToken || accountId != account(m_settings)) return;
			for (const auto &record : documents)
				m_loadedJournal.insert(record.id);
			m_workerStarted = true;
			QMetaObject::invokeMethod(
				m_worker,
				[worker = m_worker, documents] {
					for (const auto &record : documents)
						worker->accept(record.document, record.pending, record.id);
					worker->run(false);
				},
				Qt::QueuedConnection);
		});
	connect(&history, &HistoryService::syncApplied, this,
		[this](const QString &accountId, const QString &id, bool success) {
			if (accountId != account(m_settings) || !m_pendingApplies.remove(id)) return;
			if (success) {
				m_applied.insert(id);
				QMetaObject::invokeMethod(
					m_worker, [worker = m_worker, id] { worker->acknowledge(id); });
			} else if (m_runError.isEmpty())
				m_runError = tr("Could not save synchronized history.");
			finishRun();
		});
	connect(&history, &HistoryService::persistenceFailed, this, [this](const QString &error) {
		m_runError = error;
		m_status = error;
		emit statusChanged();
	});
	connect(&history, &HistoryService::published, this,
		[this](const QString &accountId, const QString &id) {
			if (accountId != account(m_settings)) return;
			m_pendingPublishes.remove(id);
			finishRun();
		});
	connect(&history, &HistoryService::publishFailed, this, [this](const QString &error) {
		m_runError = error;
		m_pendingPublishes.clear();
		if (!m_workerStarted) m_transportDone = true;
		finishRun();
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
	bool credentialsLoaded = !secret.isEmpty();
	if (secret.isEmpty()) {
		if (unchanged && m_credentialsLoaded) {
			secret = m_password;
			credentialsLoaded = true;
		} else if (settings.enabled) {
			secret = m_secrets.read(account(settings), error);
			if (!error->isEmpty()) return false;
			credentialsLoaded = true;
		}
	} else if (!m_secrets.write(account(settings), secret, error)) return false;
	QSettings preferences;
	preferences.setValue("sync/url", settings.url);
	preferences.setValue("sync/username", settings.username);
	preferences.setValue("sync/enabled", settings.enabled);
	const bool sameAccount = account(settings) == account(m_settings);
	if (!sameAccount) preferences.remove("sync/lastSuccess");
	preferences.sync();
	if (preferences.status() != QSettings::NoError) { *error = tr("Could not save sync settings."); return false; }
	m_settings = settings; m_password = secret; m_credentialsLoaded = credentialsLoaded;
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
	m_applied.clear();
	m_loadedJournal.clear();
	m_warning.clear();
	m_pendingApplies.clear();
	m_pendingPublishes.clear();
	m_runError.clear();
	m_transportDone = false;
	m_workerStarted = false;
	++m_runToken;
	const quint64 epoch = m_epoch;
	connect(m_worker, &WebDavWorker::cacheWriteFailed, this, [this, epoch] {
		// Reload both the cache and committed SQL payloads on the next attempt.
		if (epoch == m_epoch) m_configured = false;
	});
	connect(m_worker, &WebDavWorker::warning, this, [this, epoch](const QString &message) {
		if (epoch != m_epoch) return;
		m_warning = message;
		m_status = message;
		emit statusChanged();
	});
	connect(m_worker, &WebDavWorker::uploaded, this, [this, epoch](const QString &id) {
		if (epoch != m_epoch) return;
		m_pendingPublishes.insert(id);
		m_history.markPublished(account(m_settings), id);
	});
	connect(m_worker, &WebDavWorker::delivered, this, [this, epoch](const SyncDelivery &delivery) {
		if (epoch != m_epoch || !m_settings.enabled || m_applied.contains(delivery.id) ||
			m_pendingApplies.contains(delivery.id))
			return;
		m_pendingApplies.insert(delivery.id);
		m_history.applySynced(
			delivery.deleted ? HistoryEntry() : SyncContent::materialize(delivery.content),
			delivery.replaced, delivery.events, delivery.key, account(m_settings), delivery.id,
			delivery.content);
	});
	connect(m_worker, &WebDavWorker::finished, this,
		[this, epoch](const QString &error, bool success, bool probe) {
			if (epoch != m_epoch) return;
			if (probe) {
				m_busy = false;
				m_status = success
					? tr("Connection verified: read, write and delete are available.")
					: error;
				emit statusChanged();
				return;
			}
			if (!success) m_runError = error;
			m_transportDone = true;
			finishRun();
		});
	const auto settings = m_settings;
	const QString password = m_password;
	const QString directory = m_directory+"/"+account(settings);
	QMetaObject::invokeMethod(
		m_worker,
		[worker = m_worker, settings, password, directory] {
			worker->configure(settings, password, directory, true);
		},
		Qt::QueuedConnection);
	m_configured = true;
}
void WebDavSync::bootstrap(void)
{
	for (const auto &entry : m_history.entries())
		m_history.ensureDurable(entry);
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
	m_timer.stop();
	m_busy = true;
	m_runError.clear();
	m_pendingApplies.clear();
	m_pendingPublishes.clear();
	m_transportDone = false;
	m_workerStarted = false;
	m_status = probe ? tr("Testing connection…") : tr("Syncing items…");
	emit statusChanged();
	if (probe) {
		m_workerStarted = true;
		QMetaObject::invokeMethod(
			m_worker, [worker = m_worker] { worker->run(true); }, Qt::QueuedConnection);
	} else {
		m_history.retryPersistence();
		m_history.loadOutbox(account(m_settings), ++m_runToken, m_loadedJournal);
	}
}
void WebDavSync::synchronize(void) { run(false); }
void WebDavSync::testConnection(void) { run(true); }

void WebDavSync::finishRun(void)
{
	if (!m_busy || !m_transportDone || !m_pendingApplies.isEmpty() ||
		!m_pendingPublishes.isEmpty() || m_history.hasInFlightWrites())
		return;
	if (m_runError.isEmpty() && !m_history.hasPersistenceFailures() && m_again) {
		m_again = false;
		m_transportDone = false;
		m_workerStarted = false;
		m_history.loadOutbox(account(m_settings), ++m_runToken, m_loadedJournal);
		return;
	}
	m_busy = false;
	const bool success = m_runError.isEmpty() && !m_history.hasPersistenceFailures();
	if (success) {
		m_lastSuccess = QDateTime::currentDateTime();
		QSettings().setValue("sync/lastSuccess", m_lastSuccess);
		m_status = m_warning.isEmpty() ? tr("Up to date.")
									   : tr("Sync finished with skipped items: %1").arg(m_warning);
	} else
		m_status = m_runError.isEmpty() ? tr("Could not save synchronized history.") : m_runError;
	emit statusChanged();
	if (m_settings.enabled) m_timer.start(success ? 30000 : 60000);
	m_again = false;
}
