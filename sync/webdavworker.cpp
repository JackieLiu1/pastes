#include "sync/webdavworker.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
bool validId(const QString &id)
{
	const auto bytes = id.toLatin1();
	return id.size() == 64 && QByteArray::fromHex(bytes).toHex() == bytes;
}
bool writeFile(const QString &path, const QByteArray &bytes)
{
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) return false;
	file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	return file.write(bytes) == bytes.size() && file.commit();
}
}

void WebDavWorker::stop(void)
{
	m_running = false;
	if (m_reply) { m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr; }
}
QString WebDavWorker::path(const QString &id) const { return m_directory+"/events/"+id+".json"; }
QByteArray WebDavWorker::read(const QString &id)
{
	QFile file(path(id));
	if (!file.open(QIODevice::ReadOnly) || file.size() > SyncContent::maxDocumentBytes) return {};
	return file.readAll();
}
void WebDavWorker::remember(SyncContent::Record record)
{
	record.document = {}; // Payloads stay on disk; the index contains metadata only.
	m_clock = qMax(m_clock, record.modified);
	const QString previous = m_heads.value(record.key);
	if (previous.isEmpty() || m_records[previous].modified < record.modified ||
		(m_records[previous].modified == record.modified && previous < record.id))
		m_heads[record.key] = record.id;
	m_records.insert(record.id, std::move(record));
}
void WebDavWorker::configure(const SyncSettings &settings, const QString &password, const QString &directory)
{
	stop();
	m_settings = settings; m_password = password; m_directory = directory;
	m_records.clear(); m_heads.clear(); m_bindings.clear(); m_pending.clear(); m_local.clear(); m_delivered.clear(); m_error.clear();
	m_clock = 0; m_serial = 0;
	if (!m_network) m_network = new QNetworkAccessManager(this);
	if (!QDir().mkpath(m_directory+"/events")) { m_error = tr("Could not create the local sync cache."); return; }
	QFile index(m_directory+"/index.json");
	if (index.exists()) {
		if (!index.open(QIODevice::ReadOnly) || index.size() > 8*1024*1024) { m_error = tr("Could not read the local sync index."); return; }
		QJsonParseError error;
		const auto json = QJsonDocument::fromJson(index.readAll(), &error);
		if (error.error != QJsonParseError::NoError || !json.isObject()) { m_error = tr("The local sync index is damaged."); return; }
		const auto object = json.object();
		const auto mappings = object.value("bindings").toObject();
		for (auto it = mappings.begin(); it != mappings.end(); ++it)
			if (validId(it.value().toString())) m_bindings.insert(QByteArray::fromHex(it.key().toLatin1()), it.value().toString());
		for (const auto &id : object.value("pending").toArray()) if (validId(id.toString())) m_pending.insert(id.toString());
		for (const auto &id : object.value("local").toArray()) if (validId(id.toString())) m_local.insert(id.toString());
	}
	const auto files = QDir(m_directory+"/events").entryList({"*.json"}, QDir::Files);
	if (files.size() > 20000) { m_error = tr("The sync history exceeds the record limit."); return; }
	for (const auto &file : files) {
		const QString id = file.chopped(5);
		if (!validId(id)) continue;
		QString error;
		auto record = SyncContent::parse(read(id), &error);
		if (!error.isEmpty() || record.id != id) { m_error = tr("The local sync cache is damaged."); return; }
		if (SyncContent::expired(record, QDateTime::currentMSecsSinceEpoch())) {
			QFile::remove(path(id)); m_pending.remove(id); m_local.remove(id); continue;
		}
		remember(std::move(record));
	}
}
bool WebDavWorker::persistIndex(void)
{
	QJsonObject bindings;
	for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it) bindings[QString::fromLatin1(it.key().toHex())] = it.value();
	QJsonObject object{{"bindings", bindings}, {"pending", QJsonArray::fromStringList(m_pending.values())},
		{"local", QJsonArray::fromStringList(m_local.values())}};
	if (writeFile(m_directory+"/index.json", SyncContent::bytes(object))) return true;
	m_error = tr("Could not save the local sync index.");
	return false;
}
bool WebDavWorker::store(const QByteArray &bytes, bool pending)
{
	QString error;
	auto record = SyncContent::parse(bytes, &error);
	if (!error.isEmpty()) { m_error = error; return false; }
	if (!writeFile(path(record.id), bytes)) { m_error = tr("Could not save a sync record."); return false; }
	if (pending) { m_pending.insert(record.id); m_local.insert(record.id); }
	remember(std::move(record));
	return persistIndex();
}
void WebDavWorker::capture(SyncContent::Snapshot value, bool bootstrap, bool deleted, quint64 serial)
{
	m_serial = qMax(m_serial, serial);
	if (!m_error.isEmpty() || value.kind.isEmpty()) return;
	QString key = m_bindings.value(value.md5);
	if (bootstrap && !key.isEmpty() && m_heads.contains(key) &&
		m_records[m_heads[key]].modified >= value.time.toMSecsSinceEpoch()) return;
	QJsonObject payload;
	QString error;
	if (!deleted || key.isEmpty()) {
		payload = SyncContent::encode(value, &error);
		if (!error.isEmpty()) { emit warning(error); return; }
		auto decoded = SyncContent::decode(payload, &error);
		if (!error.isEmpty()) { emit warning(error); return; }
		key = SyncContent::key(decoded);
	}
	m_bindings[value.md5] = key;
	qint64 modified = bootstrap ? value.time.toMSecsSinceEpoch() : qMax(QDateTime::currentMSecsSinceEpoch(), m_clock+1);
	const auto head = m_records.value(m_heads.value(key));
	if (bootstrap && head.modified >= modified) { persistIndex(); return; }
	QJsonObject document{{"version", 1}, {"key", key}, {"modified", modified}, {"deleted", deleted}};
	if (!deleted) { document["copied"] = value.time.toMSecsSinceEpoch(); document["content"] = payload; }
	if (!store(SyncContent::bytes(document), true)) emit warning(m_error);
}
void WebDavWorker::request(const QByteArray &method, const QString &relative, const QByteArray &body, Response done)
{
	QUrl url = m_settings.url;
	url.setPath(url.path()+relative);
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	request.setTransferTimeout(15000);
	request.setRawHeader("Authorization", "Basic "+(m_settings.username+":"+m_password).toUtf8().toBase64());
	request.setRawHeader("Content-Type", method == "PROPFIND" ? "application/xml; charset=utf-8" : "application/json");
	if (method == "PROPFIND") request.setRawHeader("Depth", "1");
	if (method == "PUT") request.setRawHeader("If-None-Match", "*");
	m_reply = m_network->sendCustomRequest(request, method, body);
	m_reply->setReadBufferSize(256*1024);
	auto data = std::make_shared<QByteArray>();
	QNetworkReply *reply = m_reply;
	// Inactivity and total timeouts are separate: slow trickles cannot pin a run.
	QTimer::singleShot(30000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
	connect(reply, &QIODevice::readyRead, this, [reply,data] {
		data->append(reply->readAll());
		if (data->size() > SyncContent::maxDocumentBytes) reply->abort();
	});
	connect(reply, &QNetworkReply::finished, this, [this,reply,data,done] {
		data->append(reply->readAll());
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		m_reply = nullptr; reply->deleteLater();
		if (!m_running) return;
		if (data->size() > SyncContent::maxDocumentBytes) { complete(tr("The server response exceeds the size limit.")); return; }
		if (!status) { complete(tr("Connection failed. Check the address, certificate and network.")); return; }
		if (status >= 300 && status < 400) { complete(tr("The server redirected the request. Enter its final WebDAV address.")); return; }
		if (status == 401 || status == 403) { complete(tr("Access denied. Check the account, password and folder permissions.")); return; }
		// Errors with an HTTP status are handled by each WebDAV operation.
		done(status, *data);
	});
}
void WebDavWorker::run(bool probe)
{
	if (m_running) return;
	m_running = true; m_probe = probe;
	if (!m_error.isEmpty()) { complete(m_error); return; }
	ensureDirectory(0);
}
void WebDavWorker::ensureDirectory(int index)
{
	static const QStringList directories{"Pastes/", "Pastes/v1/", "Pastes/v1/events/"};
	if (index == directories.size()) { list(); return; }
	request("MKCOL", directories[index], {}, [this,index](int status, const QByteArray &) {
		if (status != 201 && status != 405) { complete(tr("Could not create the sync folder (HTTP %1).").arg(status)); return; }
		ensureDirectory(index+1);
	});
}
void WebDavWorker::list(void)
{
	request("PROPFIND", "Pastes/v1/events/",
		"<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/></d:prop></d:propfind>",
		[this](int status, const QByteArray &bytes) {
		if (status != 207) { complete(tr("The address is not a readable WebDAV folder (HTTP %1).").arg(status)); return; }
		QXmlStreamReader xml(bytes);
		QSet<QString> remote;
		QUrl collection = m_settings.url;
		collection.setPath(collection.path()+"Pastes/v1/events/");
		int count = 0;
		bool multistatus = false;
		while (!xml.atEnd()) {
			xml.readNext();
			if (xml.isStartElement() && xml.name() == u"multistatus" && xml.namespaceUri() == u"DAV:") multistatus = true;
			if (xml.isStartElement() && xml.name() == u"href" && xml.namespaceUri() == u"DAV:") {
				if (++count > 20001) { complete(tr("The sync history exceeds the record limit.")); return; }
				const QUrl href = collection.resolved(QUrl(xml.readElementText()));
				const QString prefix = collection.path();
				if (href.scheme() != collection.scheme() || href.authority() != collection.authority() || !href.path().startsWith(prefix)) continue;
				const QString name = href.path().mid(prefix.size());
				if (name.endsWith(".json") && validId(name.chopped(5))) remote.insert(name.chopped(5));
			}
		}
		if (xml.hasError() || !multistatus) { complete(tr("The server returned an invalid WebDAV listing.")); return; }
		if (m_probe) {
			// A connection test verifies write/read/delete, not only a successful login.
			const QString probe = "Pastes/v1/events/probe-"+QString::number(QDateTime::currentMSecsSinceEpoch())+".tmp";
			request("PUT", probe, "pastes-webdav-test", [this,probe](int code, const QByteArray &) {
				if (code != 201 && code != 204 && code != 200) { complete(tr("The WebDAV folder is not writable (HTTP %1).").arg(code)); return; }
				request("GET", probe, {}, [this,probe](int code, const QByteArray &data) {
					const bool valid = code == 200 && data == "pastes-webdav-test";
					request("DELETE", probe, {}, [this,valid](int code, const QByteArray &) {
						complete(valid && (code == 204 || code == 200) ? QString() : tr("The WebDAV read/delete check failed."));
					});
				});
			});
			return;
		}
		m_downloads.clear(); m_cleanup.clear();
		for (const auto &id : remote) {
			if (!m_records.contains(id)) m_downloads.append(id);
			else if (SyncContent::expired(m_records[id], QDateTime::currentMSecsSinceEpoch())) m_cleanup.append(id);
		}
		// Restore active cached records if a server copy was lost; never mirror
		// an empty remote folder as local deletion.
		for (const auto &id : m_heads) if (!remote.contains(id)) m_pending.insert(id);
		downloadNext();
	});
}
void WebDavWorker::downloadNext(void)
{
	if (m_downloads.isEmpty()) {
		m_uploads = m_pending.values(); uploadNext(); return;
	}
	const QString id = m_downloads.takeLast();
	request("GET", "Pastes/v1/events/"+id+".json", {}, [this,id](int status, const QByteArray &data) {
		if (status == 404) { downloadNext(); return; } // Another device expired this record.
		if (status != 200) { complete(tr("Could not download a sync record (HTTP %1).").arg(status)); return; }
		if (SyncContent::digest(data) != id) { complete(tr("The downloaded record failed its integrity check.")); return; }
		QString error;
		const auto record = SyncContent::parse(data, &error);
		if (!error.isEmpty()) { complete(error); return; }
		if (SyncContent::expired(record, QDateTime::currentMSecsSinceEpoch())) m_cleanup.append(id);
		else if (!store(data, false)) { complete(m_error); return; }
		downloadNext();
	});
}
void WebDavWorker::uploadNext(void)
{
	if (m_uploads.isEmpty()) { cleanupNext(); return; }
	const QString id = m_uploads.takeLast();
	if (!m_records.contains(id) || m_heads.value(m_records[id].key) != id ||
		SyncContent::expired(m_records[id], QDateTime::currentMSecsSinceEpoch())) {
		m_pending.remove(id); uploadNext(); return;
	}
	const QByteArray data = read(id);
	if (SyncContent::digest(data) != id) { complete(tr("Could not read an outgoing sync record.")); return; }
	request("PUT", "Pastes/v1/events/"+id+".json", data, [this,id](int status, const QByteArray &) {
		if (status != 201 && status != 204 && status != 200 && status != 412) {
			complete(tr("Could not upload a sync record (HTTP %1).").arg(status)); return;
		}
		if (status == 412) {
			request("GET", "Pastes/v1/events/"+id+".json", {}, [this,id](int code, const QByteArray &data) {
				if (code != 200 || SyncContent::digest(data) != id) {
					complete(tr("The downloaded record failed its integrity check.")); return;
				}
				m_pending.remove(id);
				if (!persistIndex()) { complete(m_error); return; }
				uploadNext();
			});
			return;
		}
		m_pending.remove(id);
		if (!persistIndex()) { complete(m_error); return; }
		uploadNext();
	});
}
void WebDavWorker::cleanupNext(void)
{
	if (m_cleanup.isEmpty()) { deliver(); return; }
	const QString id = m_cleanup.takeLast();
	request("DELETE", "Pastes/v1/events/"+id+".json", {}, [this](int status, const QByteArray &) {
		if (status != 200 && status != 204 && status != 404) { complete(tr("Could not remove expired sync data (HTTP %1).").arg(status)); return; }
		cleanupNext();
	});
}
void WebDavWorker::deliver(void)
{
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	for (const auto &id : m_heads) {
		const auto &record = m_records[id];
		if (SyncContent::expired(record, now) || m_local.contains(id) || m_delivered.contains(id)) continue;
		SyncDelivery delivery;
		delivery.id = id; delivery.deleted = record.deleted; delivery.serial = m_serial;
		for (auto it = m_bindings.begin(); it != m_bindings.end(); ++it)
			if (it.value() == record.key) delivery.replaced.append(it.key());
		if (!record.deleted) {
			QString error;
			const auto object = QJsonDocument::fromJson(read(id)).object();
			delivery.content = SyncContent::decode(object.value("content").toObject(), &error);
			if (!error.isEmpty()) { complete(error); return; }
			delivery.content.time = QDateTime::fromMSecsSinceEpoch(record.copied).toUTC();
			m_bindings[delivery.content.md5] = record.key;
			delivery.content.image = {}; // Only the original encoded bytes cross back.
		}
		emit delivered(delivery);
	}
	// Local cache follows the same retention policy, including deletion markers.
	for (auto it = m_records.begin(); it != m_records.end(); ) {
		if (SyncContent::expired(it.value(), now)) {
			if (m_heads.value(it->key) == it.key()) m_heads.remove(it->key);
			QFile::remove(path(it.key())); m_pending.remove(it.key()); m_local.remove(it.key()); m_delivered.remove(it.key());
			it = m_records.erase(it);
		} else ++it;
	}
	for (auto it = m_bindings.begin(); it != m_bindings.end(); )
		it = m_heads.contains(it.value()) ? ++it : m_bindings.erase(it);
	if (!persistIndex()) { complete(m_error); return; }
	complete();
}
void WebDavWorker::complete(const QString &error)
{
	m_running = false;
	emit finished(error, error.isEmpty(), m_probe);
}
