#include "tests/sync/davfixture.h"
#include "tests/core/entryfixtures.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QTemporaryDir>

class WebDavWorkerTestAccess
{
public:
	static QString error(const WebDavWorker &worker) { return worker.m_error; }
	static bool retain(WebDavWorker &worker, qint64 now) { return worker.prepareRetention(now); }
	static SyncContent::Record head(const WebDavWorker &worker, const QString &key)
	{
		return worker.m_records.value(worker.m_heads.value(key));
	}
};

namespace {
QByteArray read(const QString &path)
{
	QFile file(path);
	require(file.open(QIODevice::ReadOnly), "Could not read fixture");
	return file.readAll();
}
void write(const QString &path, const QByteArray &bytes)
{
	QFile file(path);
	require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
		"Could not write fixture");
}
HistoryEntry imageEntry(void)
{
	auto item = HistoryEntry::create();
	item->mimeData = std::make_unique<QMimeData>();
	QImage image(1024, 1024, QImage::Format_ARGB32);
	image.fill(Qt::green);
	item->mimeData->setImageData(image);
	item->md5 = ClipboardContent::fingerprint(*item->mimeData);
	item->time = QDateTime::currentDateTime();
	return item;
}
QByteArray document(const HistoryEntry &item)
{
	QString error;
	const auto bytes = SyncContent::document(SyncContent::snapshot(*item), false, {}, &error);
	require(error.isEmpty(), qPrintable(error));
	return bytes;
}
void cachedValidation(void)
{
	QTemporaryDir directory;
	DavServer server;
	const auto bytes = document(imageEntry());
	const auto id = SyncContent::digest(bytes);
	const auto path = directory.path()+"/events/"+id+".json";
	const auto indexPath = directory.path()+"/index.json";
	{
		WebDavWorker first;
		first.configure(server.settings(), "pass", directory.path(), false);
		first.accept(bytes);
		require(WebDavWorkerTestAccess::error(first).isEmpty(), "Initial validation failed");
	}
	const auto index = read(indexPath);
	struct AllocationLimit {
		int previous = QImageReader::allocationLimit();
		AllocationLimit(void) { QImageReader::setAllocationLimit(1); }
		~AllocationLimit(void) { QImageReader::setAllocationLimit(previous); }
	} limit;
	{
		WebDavWorker restored;
		restored.configure(server.settings(), "pass", directory.path(), false);
		require(WebDavWorkerTestAccess::error(restored).isEmpty(),
			"Unchanged validated cache decoded the large original image again");
	}
	// Unknown cache versions must fall back to full content validation.
	auto object = QJsonDocument::fromJson(index).object();
	object["validationVersion"] = -1;
	write(indexPath, SyncContent::bytes(object));
	{
		WebDavWorker older;
		older.configure(server.settings(), "pass", directory.path(), false);
		require(!WebDavWorkerTestAccess::error(older).isEmpty(),
			"An unknown validation version bypassed full image validation");
	}
	write(indexPath, index);
	write(path, bytes+" "); // Valid JSON, but no longer the validated bytes.
	{
		WebDavWorker changed;
		changed.configure(server.settings(), "pass", directory.path(), false);
		require(!WebDavWorkerTestAccess::error(changed).isEmpty(),
			"Changed cache bytes bypassed the immutable document hash");
	}
	write(path, bytes);
	// A new filename/hash is not covered by the old validation manifest.
	auto invalid = QJsonDocument::fromJson(bytes).object();
	auto content = invalid["content"].toObject();
	content["png"] = "bm90IGEgcG5n";
	invalid["content"] = content;
	const auto invalidBytes = SyncContent::bytes(invalid);
	write(directory.path()+"/events/"+SyncContent::digest(invalidBytes)+".json", invalidBytes);
	WebDavWorker added;
	added.configure(server.settings(), "pass", directory.path(), false);
	require(!WebDavWorkerTestAccess::error(added).isEmpty(),
		"New cache data bypassed full validation");
}
void unchangedPayloadsStayOnDisk(void)
{
	QTemporaryDir directory;
	DavServer server;
	const auto cache = directory.path()+"/cache";
	Client client(server.settings(), cache);
	auto image = imageEntry();
	client.capture(image);
	client.run();
	require(client.success && client.deliveries.size() == 1, "Initial image sync failed");
	const auto id = client.deliveries.first().id;
	QMetaObject::invokeMethod(client.worker, [worker = client.worker, id] { worker->acknowledge(id); });
	// Make the old bodies unavailable. Unchanged polls and an unrelated new
	// item must use the in-memory metadata, without reopening old payloads.
	require(QDir().rename(cache+"/events", cache+"/held"), "Cannot hold old payloads");
	require(QDir().mkpath(cache+"/events"), "Cannot create new payload directory");
	const int puts = server.puts, gets = server.gets;
	for (int round = 0; round < 3; ++round) {
		client.run();
		require(client.success && client.deliveries.isEmpty() && server.puts == puts &&
				server.gets == gets, "Unchanged poll reread or retransmitted old content");
	}
	client.capture(textEntry("independent new content"));
	client.run();
	require(client.success && client.deliveries.size() == 1 &&
			client.deliveries.first().content.text == "independent new content",
		"Changing one key required unrelated payloads");
	for (const auto &name : QDir(cache+"/held").entryList(QDir::Files))
		require(QFile::rename(cache+"/held/"+name, cache+"/events/"+name), "Cannot restore payload");
	image->time = image->time.addSecs(1);
	client.capture(image);
	client.run();
	bool updated = false;
	for (const auto &delivery : client.deliveries)
		updated |= delivery.content.kind == "image" && delivery.content.time == image->time;
	require(client.success && updated, "A changed image key did not invalidate its previous merge");
}
void expirationWithoutNewEvents(void)
{
	QTemporaryDir directory;
	DavServer server;
	const auto now = QDateTime::currentMSecsSinceEpoch();
	const auto copied = now-30LL*86400000+1000;
	auto item = textEntry("expired unstar", QDateTime::fromMSecsSinceEpoch(copied));
	item->favoriteModified = now-1000;
	const auto bytes = document(item);
	const auto key = SyncContent::key(SyncContent::snapshot(*item));
	WebDavWorker worker;
	worker.configure(server.settings(), "pass", directory.path(), false);
	worker.accept(bytes);
	require(WebDavWorkerTestAccess::retain(worker, now), "Initial reduction failed");
	require(!WebDavWorkerTestAccess::head(worker, key).deleted, "Content expired too early");
	require(WebDavWorkerTestAccess::retain(worker, now-5000), "Clock rollback failed");
	require(!WebDavWorkerTestAccess::head(worker, key).deleted, "Clock rollback deleted content");
	require(WebDavWorkerTestAccess::retain(worker, now+1000), "Timed expiry failed");
	const auto tombstone = WebDavWorkerTestAccess::head(worker, key);
	require(tombstone.deleted && tombstone.favoriteModified &&
			!SyncContent::expired(tombstone, now+365LL*86400000),
		"A clean group did not create its durable favorite removal marker at expiry");
}
void batchedUploadProgress(void)
{
	QTemporaryDir directory;
	DavServer server;
	const auto cache = directory.path()+"/cache";
	const auto indexPath = cache+"/index.json";
	QByteArray staleIndex;
	int pendingDuringSecondPut = -1;
	server.onRequest = [&](const QByteArray &method) {
		if (method != "PUT") return;
		if (!server.puts) staleIndex = read(indexPath);
		if (server.puts == 1) {
			pendingDuringSecondPut = QJsonDocument::fromJson(read(indexPath))
				.object()["pending"].toArray().size();
			server.failPut = true;
		}
	};
	{
		Client client(server.settings(), cache);
		for (int index = 0; index < 3; ++index)
			client.capture(textEntry(QString("batch item %1").arg(index)));
		client.run();
		require(!client.success && server.files.size() == 1, "Partial upload fixture did not fail");
		require(pendingDuringSecondPut == 3, "Upload rewrote the index before the batch finished");
		require(QJsonDocument::fromJson(read(indexPath)).object()["pending"].toArray().size() == 2,
			"Failed batch did not save completed upload progress");
	}
	server.onRequest = {};
	{
		Client restarted(server.settings(), cache);
		restarted.run();
		require(restarted.success && server.files.size() == 3 && server.puts == 4,
			"Restart did not resume the remaining partial batch");
	}
	// A crash after PUT but before the final index commit can leave old pending
	// IDs. Replaying that index must safely verify the immutable remote bytes.
	write(indexPath, staleIndex);
	const int gets = server.gets;
	Client interrupted(server.settings(), cache);
	interrupted.run();
	require(interrupted.success && server.files.size() == 3 && server.gets == gets+3 &&
			QJsonDocument::fromJson(read(indexPath)).object()["pending"].toArray().isEmpty(),
		"An interrupted index commit did not recover through idempotent uploads");
}
void uploadIndexFailure(void)
{
	QTemporaryDir directory;
	DavServer server;
	const auto cache = directory.path()+"/cache";
	const auto indexPath = cache+"/index.json";
	bool blocked = false;
	server.onRequest = [&](const QByteArray &method) {
		if (method == "PUT" && !blocked)
			blocked = QFile::rename(indexPath, indexPath+".saved") && QDir().mkpath(indexPath);
	};
	{
		Client client(server.settings(), cache);
		client.capture(textEntry("upload with failed index commit"));
		client.run();
		require(blocked && server.files.size() == 1 && !client.success && !client.error.isEmpty(),
			"A failed final index write was reported as successful sync");
	}
	server.onRequest = {};
	require(QDir().rmdir(indexPath) && QFile::rename(indexPath+".saved", indexPath),
		"Cannot restore the index fixture");
	Client restarted(server.settings(), cache);
	restarted.run();
	require(restarted.success && server.files.size() == 1,
		"A failed index write prevented safe restart and retry");
}
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	qRegisterMetaType<SyncDelivery>();
	int failures = runTest("reuse validation only for unchanged cached bytes", cachedValidation);
	failures += runTest("unchanged and unrelated groups do not read old payloads", unchangedPayloadsStayOnDisk);
	failures += runTest("expiry and clock rollback without new events", expirationWithoutNewEvents);
	failures += runTest("batch upload index progress and interrupted restart", batchedUploadProgress);
	failures += runTest("failed upload index commit reports failure and retries", uploadIndexFailure);
	return failures ? 1 : 0;
}
