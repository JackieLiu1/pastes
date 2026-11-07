#include "tests/sync/davfixture.h"
#include "tests/core/entryfixtures.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>

class WebDavWorkerTestAccess
{
public:
	static QString error(const WebDavWorker &worker) { return worker.m_error; }
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
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	qRegisterMetaType<SyncDelivery>();
	int failures = runTest("reuse validation only for unchanged cached bytes", cachedValidation);
	return failures ? 1 : 0;
}
