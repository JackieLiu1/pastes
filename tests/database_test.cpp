#include "tests/testsupport.h"
#include "storage/database.h"
#include "core/clipboarddata.h"
#include <QCoreApplication>
#include <QTemporaryDir>

namespace {

QList<HistoryEntry> read(Database &repository)
{
	QList<HistoryEntry> entries;
	bool loaded = false;
	const auto connection = QObject::connect(&repository, &HistoryRepository::loaded, &repository,
		[&](const QList<HistoryEntry> &result) { entries = result; loaded = true; });
	repository.load();
	waitUntil([&] { return loaded; });
	QObject::disconnect(connection);
	return entries;
}

void persistenceAndShutdown(void)
{
	QTemporaryDir directory;
	require(directory.isValid(), "Temporary database directory unavailable");
	const QString path = directory.filePath("nested/PastesDatabase.db");
	QByteArray textHash;
	QImage icon(8, 8, QImage::Format_ARGB32); icon.fill(Qt::green);
	{
		Database repository(path);
		auto text = textEntry("queued at shutdown");
		textHash = text->md5;
		text->mimeData->setData("application/custom", QByteArray("a\0b", 3));
		repository.insert(text);
		/* Mutating and releasing the original must not affect queued SQL. */
		text->mimeData->setText("changed after request");
		text.clear();
		repository.updateIcon(textHash, icon);
		for (int i = 0; i < 25; ++i) repository.insert(textEntry(QString::number(i)));
		/* Deliberately destroy without running the GUI event loop. */
	}
	{
		Database repository(path);
		const auto entries = read(repository);
		require(entries.size() == 26, "Shutdown dropped pending inserts");
		HistoryEntry text;
		for (const auto &entry : entries) if (entry->md5 == textHash) text = entry;
		require(text && text->mimeData->text() == "queued at shutdown", "Worker read caller-mutated MIME data");
		require(text->mimeData->data("application/custom") == QByteArray("a\0b", 3), "Binary MIME data changed");
		require(text->icon.pixelColor(0, 0) == QColor(Qt::green), "Queued source icon update was lost");
		repository.remove(textHash);
		require(read(repository).size() == 25, "Removal was not ordered before load");
	}
}

void imagesAndConnections(void)
{
	QTemporaryDir directory;
	Database first(directory.filePath("first.db"));
	Database second(directory.filePath("second.db"));
	QStringList errors;
	QObject::connect(&first, &HistoryRepository::failed, &first, [&](const QString &error) { errors.append(error); });
	auto imageEntry = HistoryEntry::create();
	imageEntry->mimeData = new QMimeData;
	QImage image(72, 32, QImage::Format_ARGB32); image.fill(Qt::red);
	imageEntry->mimeData->setImageData(image);
	imageEntry->md5 = ClipboardContent::fingerprint(*imageEntry->mimeData);
	imageEntry->time = QDateTime::currentDateTime();
	bool encoded = false;
	quint64 encodedRequest = 0;
	int encodedFormat = QImage::Format_Invalid;
	QByteArray encodedBytes;
	QObject::connect(&first, &HistoryRepository::imageEncoded, &first,
		[&](quint64 request, const QByteArray &bytes, int format, qreal) {
		encodedRequest = request;
		encodedFormat = format;
		encodedBytes = bytes;
		encoded = true;
	});
	first.insert(imageEntry);
	imageEntry.clear();
	const auto entries = read(first);
	waitUntil([&] { return encoded; });
	require(encodedRequest == 1 && encodedFormat == image.format(), "Encoding completion identity changed");
	require(QImage::fromData(encodedBytes).pixelColor(0, 0) == QColor(Qt::red), "Encoded image changed");
	require(errors.isEmpty(), "SQL reported an error");
	require(entries.size() == 1 && !ClipboardData::storedImage(entries.first()->mimeData).isEmpty(), "Loaded image did not stay encoded");
	QSize originalSize;
	const QImage preview = ClipboardData::previewImage(entries.first()->mimeData, &originalSize);
	require(originalSize == image.size() && !preview.isNull(), "Loaded image preview changed");
	require(read(second).isEmpty(), "Independent repositories shared a connection");
}

}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	failures += runTest("queued value snapshots and shutdown", persistenceAndShutdown);
	failures += runTest("encoded images and independent connections", imagesAndConnections);
	return failures ? 1 : 0;
}
