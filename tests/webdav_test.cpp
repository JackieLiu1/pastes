#include "tests/testsupport.h"
#include "sync/webdavsync.h"
#include "core/clipboarddata.h"
#include "platform/secretstore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QFile>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QJsonDocument>

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
						++listings;
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
	int puts = 0, gets = 0, listings = 0;
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

void mixedImageSync(void)
{
	DavServer server; QTemporaryDir temp;
	Client first(server.settings(), temp.path()+"/a");
	QImage image(48, 32, QImage::Format_RGB32); image.fill(Qt::green);
	auto entry = textEntry("");
	const QUrl url = QUrl::fromLocalFile(temp.filePath("missing temporary image.png"));
	entry->mimeData->setUrls({url}); entry->mimeData->setImageData(image);
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	auto *stored = ClipboardData::withStoredImage(entry->mimeData, png(image), image.format());
	delete entry->mimeData; entry->mimeData = stored;
	const auto snapshot = SyncContent::snapshot(*entry);
	require(snapshot.kind == "image" && snapshot.urls.isEmpty() && snapshot.png == png(image),
		"An accompanying file URL excluded supplied image content from sync");
	first.capture(entry); first.run();
	require(first.success && server.files.size() == 1, "Mixed clipboard image was not uploaded as one item");
	for (const QByteArray &record : server.files) {
		const QJsonObject content = QJsonDocument::fromJson(record).object().value("content").toObject();
		require(content.value("kind").toString() == "image" && !content.contains("urls") &&
			!record.contains("missing temporary image.png") && !record.contains("file://"),
			"Image synchronization exported the accompanying local file path");
	}
	Client second(server.settings(), temp.path()+"/b"); second.run();
	require(second.success && second.deliveries.size() == 1, "Mixed clipboard image did not reach the other device");
	const auto received = second.deliveries.first().content;
	const auto restored = SyncContent::materialize(received);
	const QImage receivedImage = qvariant_cast<QImage>(restored->mimeData->imageData());
	require(received.kind == "image" && received.urls.isEmpty() && receivedImage.size() == image.size() &&
		receivedImage.pixelColor(0, 0) == image.pixelColor(0, 0), "Mixed clipboard image lost its pixels in transit");
	entry->mimeData->setUrls({QUrl::fromLocalFile("/tmp/document.docx")});
	require(SyncContent::snapshot(*entry).kind.isEmpty(), "A file icon entered image synchronization");
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
		config.close(); // Allow QSettings to replace the INI file on Windows.
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

void sourceIconRoundTrip()
{
	for (int size : {16, 32, 64, 128}) {
		auto entry = textEntry("source icon");
		entry->icon = QImage(size, size, QImage::Format_ARGB32);
		entry->icon.fill(Qt::green); entry->icon.setPixelColor(3, 5, Qt::red);
		QString error;
		const auto decoded = SyncContent::decode(SyncContent::encode(SyncContent::snapshot(*entry), &error), &error);
		require(error.isEmpty() && decoded.icon == entry->icon, "Sync enlarged or resampled a source icon");
	}
	auto large = textEntry("large icon");
	large->icon = QImage(256, 192, QImage::Format_ARGB32); large->icon.fill(Qt::blue);
	QString error;
	const auto decoded = SyncContent::decode(SyncContent::encode(SyncContent::snapshot(*large), &error), &error);
	require(error.isEmpty() && decoded.icon.size() == QSize(128, 96), "Oversized source icon was not bounded");

	/* A later source lookup upgrades the same item without changing its
	 * content identity, copy time or number of visible history entries. */
	DavServer server; QTemporaryDir directory;
	Client first(server.settings(), directory.path()+"/a"), second(server.settings(), directory.path()+"/b");
	auto item = textEntry("icon upgrade", QDateTime::currentDateTime().addSecs(-30));
	item->icon = QImage(16, 16, QImage::Format_ARGB32); item->icon.fill(Qt::green);
	first.capture(item); first.run(); second.run();
	require(first.success && second.success && second.deliveries.size() == 1, "Initial source icon sync failed");
	MemoryRepository repository; HistoryService history(repository);
	history.load(); repository.finishLoad();
	const auto initial = second.deliveries.first();
	history.mergeSynced(SyncContent::materialize(initial.content), initial.replaced);
	const auto entryId = history.entries().first()->id;
	item->icon = QImage(128, 128, QImage::Format_ARGB32); item->icon.fill(Qt::blue);
	item->icon.setPixelColor(7, 8, Qt::white);
	first.capture(item); first.run(); second.run();
	require(first.success && second.success && second.deliveries.size() == 1, "High-resolution icon upgrade was not delivered");
	const auto updated = second.deliveries.first();
	require(updated.content.icon == item->icon && updated.content.time == item->time,
		"Icon upgrade changed pixels or the original copy time");
	history.mergeSynced(SyncContent::materialize(updated.content), updated.replaced);
	require(history.entries().size() == 1 && history.entries().first()->id == entryId &&
		history.entries().first()->icon == item->icon, "Icon upgrade replaced or duplicated the visible item");
}

void localCredentialFiles()
{
	QTemporaryDir temp;
	const QString directory = temp.path()+"/credentials";
	auto store = Platform::createLocalSecretStore(directory);
	QString error;
	require(store->read("first", &error).isEmpty() && !error.isEmpty(), "Missing password did not explain how to save it");
	error.clear();
	const QString secret = QString::fromUtf8("test-only 密码 \"with\\escapes\"\n");
	require(store->write("first", secret, &error) && error.isEmpty(), "Could not save local credential fixture");
	require(store->write("../../second", "other-test-secret", &error), "Could not save isolated account");
	require(store->read("first", &error) == secret && store->read("../../second", &error) == "other-test-secret",
		"Account switching mixed up saved passwords");
	const auto files = QDir(directory).entryList(QDir::Files);
	require(files.size() == 2 && QDir(temp.path()).entryList(QDir::Files).isEmpty(), "Account escaped the credential directory");
#ifdef Q_OS_UNIX
	const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
		QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
	require(!(QFileInfo(directory).permissions() & publicPermissions), "Credential directory is accessible to other users");
	for (const auto &file : files)
		require(!(QFileInfo(directory+'/'+file).permissions() & publicPermissions), "Credential file is accessible to other users");
#endif
	/* A new process must recover the saved password without system prompts. */
	QProcess restart;
	restart.start(QCoreApplication::applicationFilePath(), {"--read-local-password", directory});
	require(restart.waitForFinished(5000) && restart.exitStatus() == QProcess::NormalExit && restart.exitCode() == 0,
		"Restart did not recover the locally saved password");
	require(!store->write("first", QString(70*1024, 'x'), &error) && !error.isEmpty(), "Oversized credential unexpectedly saved");
	error.clear();
	require(store->read("first", &error) == secret && error.isEmpty(), "Failed save destroyed the previous password");
	store.reset();
	store = Platform::createLocalSecretStore(directory);
	require(store->write("first", "replacement", &error) && store->read("first", &error) == "replacement",
		"Password replacement did not persist");
	const QString damagedAccount = "first";
	const QString damaged = directory+'/'+QString::fromLatin1(
		QCryptographicHash::hash(damagedAccount.toUtf8(), QCryptographicHash::Sha256).toHex())+".json";
	QFile file(damaged); require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot prepare damaged credential");
	file.write("{broken"); file.close();
	require(store->read(damagedAccount, &error).isEmpty() && !error.isEmpty(), "Damaged local credential was accepted");
	QFile obstacle(temp.path()+"/not-a-directory"); require(obstacle.open(QIODevice::WriteOnly), "Cannot prepare write failure"); obstacle.close();
	auto blocked = Platform::createLocalSecretStore(obstacle.fileName()+"/credentials");
	error.clear(); require(!blocked->write("first", secret, &error) && !error.isEmpty(), "Write failure was silently accepted");
#ifdef Q_OS_UNIX
	/* Never follow an unexpected link while tightening permissions or saving. */
	require(QFile::remove(damaged) && QFile::link(obstacle.fileName(), damaged), "Cannot prepare linked credential");
	error.clear(); require(store->read(damagedAccount, &error).isEmpty() && !error.isEmpty(), "Read followed a credential symlink");
	error.clear(); require(!store->write(damagedAccount, secret, &error) && !error.isEmpty(), "Write followed a credential symlink");
	require(QFileInfo(obstacle.fileName()).size() == 0, "Credential operation modified a symlink target");
#endif
}

void localCredentialSyncRestart()
{
	DavServer server; QTemporaryDir temp;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path()+"/preferences");
	const QString directory = temp.path()+"/credentials";
	{
		auto secrets = Platform::createLocalSecretStore(directory);
		MemoryRepository repository; HistoryService history(repository);
		WebDavSync service(history, *secrets, temp.path()+"/cache");
		history.load(); repository.finishLoad({textEntry("local credential sync fixture")});
		QString error;
		require(!service.save(server.settings(), {}, &error), "Sync enabled without a saved local password");
		auto disabled = server.settings(); disabled.enabled = false;
		error.clear(); require(service.save(disabled, {}, &error), "Disabling sync required a missing password");
		require(service.save(server.settings(), "pass", &error), "Could not save a local sync password");
		service.synchronize(); waitUntil([&] { return !service.busy(); });
		require(service.lastSuccess().isValid() && server.files.size() == 1, "Local password was not used for WebDAV authentication");
	}
	{
		auto secrets = Platform::createLocalSecretStore(directory);
		MemoryRepository repository; HistoryService history(repository);
		WebDavSync service(history, *secrets, temp.path()+"/cache");
		history.load(); repository.finishLoad();
		const int previousListings = server.listings;
		/* Let the normal startup timer synchronize; no save or sync command. */
		waitUntil([&] { return server.listings > previousListings && !service.busy(); }, 5000);
		require(service.lastSuccess().isValid(), "Startup could not auto-sync with the local password");
		QString error;
		require(service.save(server.settings(), {}, &error), "Blank password did not preserve saved credentials");
		QFile settings(QSettings().fileName()); require(settings.open(QIODevice::ReadOnly), "Missing saved settings");
		require(!settings.readAll().contains("pass"), "Credential leaked into ordinary settings");
		for (const auto &record : server.files)
			require(!record.contains("pass"), "Credential leaked into a WebDAV record");
	}
}
}
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() == 3 && app.arguments()[1] == "--read-local-password") {
		auto store = Platform::createLocalSecretStore(app.arguments()[2]);
		QString error;
		return store->read("first", &error) == QString::fromUtf8("test-only 密码 \"with\\escapes\"\n") && error.isEmpty() ? 0 : 1;
	}
	QCoreApplication::setOrganizationName("PastesSyncTests");
	QCoreApplication::setApplicationName("Contracts");
	qRegisterMetaType<SyncDelivery>();
	int failures = 0;
	failures += runTest("per-item incremental merge and deletion", incrementalAndDelete);
	failures += runTest("offline restart and concurrent devices", offlineRestartAndConcurrency);
	failures += runTest("simultaneous independent item uploads", simultaneousWriters);
	failures += runTest("file exclusion and server validation", limitsAndIntegrity);
	failures += runTest("supplied clipboard images sync without accompanying local paths", mixedImageSync);
	failures += runTest("service settings and disabled-session persistence", servicePersistence);
	failures += runTest("image content and timestamp round trip", imageRoundTrip);
	failures += runTest("source icon resolution and incremental upgrades", sourceIconRoundTrip);
	failures += runTest("private local credential files and process restart", localCredentialFiles);
	failures += runTest("saved local credentials resume automatic sync", localCredentialSyncRestart);
	return failures ? 1 : 0;
}
