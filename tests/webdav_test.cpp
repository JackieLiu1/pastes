#include "tests/testsupport.h"
#include "sync/webdavsync.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QSettings>

namespace {
class DavServer final : public QTcpServer
{
public:
	DavServer()
	{
		require(listen(QHostAddress::LocalHost), "Cannot listen on local WebDAV test port");
		connect(this, &QTcpServer::newConnection, this, [this] {
			while (hasPendingConnections()) {
				auto *socket = nextPendingConnection();
				auto bytes = std::make_shared<QByteArray>();
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				connect(socket, &QTcpSocket::readyRead, this, [this,socket,bytes] {
					bytes->append(socket->readAll());
					const int end = bytes->indexOf("\r\n\r\n");
					if (end < 0) return;
					const auto headers = bytes->left(end).split('\n');
					const auto request = headers.first().trimmed().split(' ');
					QHash<QByteArray,QByteArray> fields;
					for (const auto &line : headers) {
						const int colon = line.indexOf(':');
						if (colon > 0) fields[line.left(colon).trimmed().toLower()] = line.mid(colon+1).trimmed();
					}
					const int length = fields.value("content-length").toInt();
					if (bytes->size() < end+4+length) return;
					const QByteArray body = bytes->mid(end+4, length);
					const QByteArray method = request.value(0);
					const QString path = QString::fromUtf8(request.value(1));
					int status = 200; QByteArray response;
					if (fields.value("authorization") != "Basic dXNlcjpwYXNz") status = 401;
					else if (redirect) status = 302;
					else if (method == "MKCOL") status = 201;
					else if (method == "PROPFIND") {
						status = 207; response = "<d:multistatus xmlns:d=\"DAV:\">";
						for (auto it = files.begin(); it != files.end(); ++it)
							response += "<d:response><d:href>"+it.key().toUtf8()+"</d:href></d:response>";
						response += "</d:multistatus>";
						if (badListing) response = "<html>login required</html>";
					} else if (method == "GET") {
						++gets;
						if (!files.contains(path)) status = 404;
						else response = corrupt ? QByteArray("{}") : files.value(path);
					} else if (method == "PUT") {
						++puts;
						if (failPut) { failPut = false; status = 503; }
						else if (files.contains(path) && fields.value("if-none-match") == "*") status = 412;
						else { files[path] = body; status = 201; }
					} else if (method == "DELETE") {
						status = files.remove(path) ? 204 : 404;
					} else status = 405;
					socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nConnection: close\r\nContent-Length: "+QByteArray::number(response.size())+"\r\n\r\n"+response);
					socket->disconnectFromHost();
				});
			}
		});
	}
	SyncSettings settings() const { return {QUrl(QString("http://127.0.0.1:%1/").arg(serverPort())), "user", true}; }
	QHash<QString,QByteArray> files;
	int puts = 0, gets = 0;
	bool failPut = false, corrupt = false, redirect = false, badListing = false;
};
class Client final : public QObject
{
public:
	Client(const SyncSettings &settings, const QString &directory, const QString &password = "pass")
	{
		worker = new WebDavWorker;
		worker->moveToThread(&thread);
		connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
		connect(worker, &WebDavWorker::delivered, this, [this](const SyncDelivery &value) { deliveries.append(value); });
		connect(worker, &WebDavWorker::finished, this, [this](const QString &message, bool ok, bool) { ++finished; success = ok; error = message; });
		thread.start();
		QMetaObject::invokeMethod(worker, [=] { worker->configure(settings, password, directory); });
	}
	~Client() { QMetaObject::invokeMethod(worker, &WebDavWorker::stop, Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); }
	void capture(const HistoryEntry &entry, bool bootstrap = false, bool deleted = false)
	{
		const auto value = SyncContent::snapshot(*entry);
		const quint64 sequence = ++serial;
		QMetaObject::invokeMethod(worker, [=] { worker->capture(value, bootstrap, deleted, sequence); });
	}
	void run(bool probe = false)
	{
		const int previous = finished;
		deliveries.clear();
		QMetaObject::invokeMethod(worker, [=] { worker->run(probe); });
		waitUntil([&] { return finished > previous; }, 10000);
	}
	QThread thread;
	WebDavWorker *worker;
	QList<SyncDelivery> deliveries;
	quint64 serial = 0;
	int finished = 0;
	bool success = false;
	QString error;
};
void incrementalAndDelete()
{
	DavServer server; QTemporaryDir temp;
	Client first(server.settings(), temp.path()+"/a"), second(server.settings(), temp.path()+"/b");
	auto old = textEntry("first item", QDateTime::currentDateTime().addSecs(-100));
	auto newer = textEntry("second item", QDateTime::currentDateTime().addSecs(-20));
	first.capture(old, true); first.capture(newer, true); first.run();
	require(first.success && server.files.size() == 2, "Items were not stored independently");
	second.run();
	require(second.success && second.deliveries.size() == 2, "Second device did not restore both items");
	MemoryRepository repository; HistoryService history(repository); history.load(); repository.finishLoad();
	for (const auto &delivery : second.deliveries) history.mergeSynced(SyncContent::materialize(delivery.content), delivery.replaced);
	require(history.entries().size() == 2 && history.entries().first()->time == newer->time, "History was not restored in original time order");
	const int puts = server.puts, gets = server.gets;
	first.run(); second.run();
	require(first.success && second.success && server.puts == puts && server.gets == gets, "Unchanged item bodies were retransmitted");
	first.capture(old, false, true); first.run(); second.run();
	for (const auto &delivery : second.deliveries)
		history.mergeSynced(delivery.deleted ? HistoryEntry() : SyncContent::materialize(delivery.content), delivery.replaced);
	require(history.entries().size() == 1 && history.entries().first()->md5 == newer->md5, "Remote deletion did not remove the intended item");
	// A new device still holding an old local copy must not resurrect it.
	Client stale(server.settings(), temp.path()+"/stale"); stale.capture(old, true); stale.run();
	require(stale.success, "Stale-device reconciliation failed");
	bool deleted = false;
	for (const auto &delivery : stale.deliveries) deleted |= delivery.deleted;
	require(deleted, "Old local history overrode the deletion marker");
	first.capture(old); first.run(); second.run();
	bool restored = false;
	for (const auto &delivery : second.deliveries) if (!delivery.deleted && delivery.content.text == "first item") restored = true;
	require(restored, "Explicit re-copy did not supersede deletion");
}
void offlineRestartAndConcurrency()
{
	DavServer server; QTemporaryDir temp;
	auto a = textEntry("offline item");
	{
		Client first(server.settings(), temp.path()+"/a");
		server.failPut = true; first.capture(a); first.run();
		require(!first.success && server.files.isEmpty(), "Failed upload was reported as successful");
	}
	Client first(server.settings(), temp.path()+"/a"), second(server.settings(), temp.path()+"/b");
	second.capture(textEntry("other device item")); second.run(); first.run(); second.run();
	require(first.success && second.success && server.files.size() == 2, "Concurrent/offline items were overwritten");
	bool found = false;
	for (const auto &delivery : second.deliveries) found |= delivery.content.text == "offline item";
	require(found, "Pending item did not survive process restart");
}
void simultaneousWriters()
{
	DavServer server; QTemporaryDir temp;
	Client first(server.settings(), temp.path()+"/a"), second(server.settings(), temp.path()+"/b");
	first.capture(textEntry("simultaneous A")); second.capture(textEntry("simultaneous B"));
	QMetaObject::invokeMethod(first.worker, [&] { first.worker->run(false); });
	QMetaObject::invokeMethod(second.worker, [&] { second.worker->run(false); });
	waitUntil([&] { return first.finished && second.finished; }, 10000);
	require(first.success && second.success && server.files.size() == 2, "Simultaneous writers lost an item");
	first.run(); second.run();
	require(first.success && second.success && !first.deliveries.isEmpty() && !second.deliveries.isEmpty(), "Devices did not converge after simultaneous writes");
}

void limitsAndIntegrity()
{
	DavServer server; QTemporaryDir temp;
	Client first(server.settings(), temp.path()+"/a");
	auto file = textEntry("file"); file->mimeData->setUrls({QUrl::fromLocalFile("/tmp/private-file")});
	require(SyncContent::snapshot(*file).kind.isEmpty(), "File references must stay local");
	first.capture(textEntry("valid item")); first.run(); require(first.success, "Initial upload failed");
	Client second(server.settings(), temp.path()+"/b");
	server.corrupt = true; second.run();
	require(!second.success && second.deliveries.isEmpty(), "Corrupt remote item reached history");
	server.corrupt = false; server.badListing = true; second.run();
	require(!second.success, "HTML login response was accepted as WebDAV");
	server.badListing = false; server.redirect = true; second.run();
	require(!second.success, "Redirect was followed with credentials");
	server.redirect = false;
	Client wrong(server.settings(), temp.path()+"/wrong", "incorrect"); wrong.run();
	require(!wrong.success, "Invalid credentials were accepted");
	QString error; SyncSettings settings{QUrl("http://example.com/webdav/"), "user", true};
	require(!WebDavSync::validate(&settings, &error), "Plain HTTP was allowed outside loopback");
	first.run(true); require(first.success && server.files.size() == 1, "Connection probe left files or failed");
}
class MemorySecrets final : public SecretStore
{
public:
	QString read(const QString &account, QString *) override { return values.value(account); }
	bool write(const QString &account, const QString &secret, QString *) override { values[account] = secret; return true; }
	QHash<QString,QString> values;
};
void servicePersistence()
{
	DavServer server; QTemporaryDir temp;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path()+"/preferences");
	MemorySecrets secrets;
	auto item = textEntry("service item");
	{
		MemoryRepository repository; HistoryService history(repository);
		WebDavSync service(history, secrets, temp.path()+"/cache");
		history.load(); repository.finishLoad({item});
		QString error;
		require(service.save(server.settings(), "pass", &error), "Service settings could not be saved");
		service.synchronize(); waitUntil([&] { return !service.busy(); });
		require(service.lastSuccess().isValid() && server.files.size() == 1, "Service did not bootstrap local history");
		QFile config(QSettings().fileName()); require(config.open(QIODevice::ReadOnly), "Test settings missing");
		require(!config.readAll().contains("pass"), "Password leaked into ordinary preferences");
		auto disabled = server.settings(); disabled.enabled = false;
		require(service.save(disabled, {}, &error), "Could not disable sync");
	}
	// Restart while disabled; a local deletion still needs a durable marker.
	{
		MemoryRepository repository; HistoryService history(repository);
		WebDavSync service(history, secrets, temp.path()+"/cache");
		history.load(); repository.finishLoad({cloneEntry(*item)});
		history.remove(history.entries().first()->id);
		QString error;
		require(service.save(server.settings(), {}, &error), "Could not restore saved credentials");
		service.synchronize(); waitUntil([&] { return !service.busy(); });
		require(service.lastSuccess().isValid(), "Service could not reconnect after restart");
	}
	Client other(server.settings(), temp.path()+"/other"); other.capture(item, true); other.run();
	bool deleted = false;
	for (const auto &delivery : other.deliveries) deleted |= delivery.deleted;
	require(deleted, "Disabled-session deletion was lost on restart/re-enable");
}

void imageRoundTrip()
{
	DavServer server; QTemporaryDir temp;
	Client first(server.settings(), temp.path()+"/a"), second(server.settings(), temp.path()+"/b");
	auto item = HistoryEntry::create(); item->mimeData = new QMimeData; item->time = QDateTime::currentDateTime();
	QImage image(80, 60, QImage::Format_ARGB32); image.fill(QColor(11, 129, 78, 200));
	item->mimeData->setImageData(image); item->md5 = ClipboardContent::fingerprint(*item->mimeData);
	first.capture(item); first.run(); second.run();
	require(second.success && second.deliveries.size() == 1, "Image was not synced");
	const auto received = SyncContent::materialize(second.deliveries.first().content);
	const QImage decoded = qvariant_cast<QImage>(received->mimeData->imageData());
	require(decoded.size() == image.size() && decoded.pixelColor(0,0) == image.pixelColor(0,0), "Image pixels changed");
	require(received->time == item->time, "Original copy time was changed");
}
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	QCoreApplication::setOrganizationName("PastesSyncTests");
	QCoreApplication::setApplicationName("Contracts");
	qRegisterMetaType<SyncDelivery>();
	int failures = 0;
	failures += runTest("per-item incremental merge and deletion", incrementalAndDelete);
	failures += runTest("offline restart and concurrent devices", offlineRestartAndConcurrency);
	failures += runTest("simultaneous independent item uploads", simultaneousWriters);
	failures += runTest("file exclusion and server validation", limitsAndIntegrity);
	failures += runTest("service settings and disabled-session persistence", servicePersistence);
	failures += runTest("image content and timestamp round trip", imageRoundTrip);
	return failures ? 1 : 0;
}
