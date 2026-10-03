#include "tests/testsupport.h"
#include "storage/database.h"
#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QUrl>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

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

void favoritesPersistence(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath("favorites.db");
	QByteArray savedHash;
	const auto copied = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch()).addDays(-45);
	const qint64 modified = QDateTime::currentMSecsSinceEpoch();
	{
		Database repository(path);
		auto entry = textEntry("favorite value snapshot", copied);
		entry->favorite = true; entry->favoriteModified = modified;
		entry->favoriteDetails = {"saved name", modified, 2048, modified};
		savedHash = entry->md5;
		repository.insert(entry);
		entry->favorite = false; entry->favoriteDetails.name = "caller mutation";
		repository.insert(textEntry("ordinary"));
	}
	{
		Database repository(path);
		auto entries = read(repository);
		HistoryEntry saved;
		for (const auto &entry : entries) if (entry->md5 == savedHash) saved = entry;
		require(saved && saved->favorite && saved->favoriteModified == modified && saved->time == copied &&
			saved->favoriteDetails == FavoriteDetails({"saved name", modified, 2048, modified}),
			"Favorite snapshot, original copy time or restart metadata changed");
		FavoriteDetails details{"updated", modified+1, 1024, modified+1};
		repository.updateFavorite(savedHash, false, modified+1, details);
		details.name = "changed after queueing";
	}
	{
		Database repository(path);
		const auto entries = read(repository);
		for (const auto &entry : entries) if (entry->md5 == savedHash)
			require(!entry->favorite && entry->favoriteModified == modified+1 && entry->favoriteDetails.name == "updated" &&
				entry->favoriteDetails.position == 1024,
				"Unfavorite state was lost at shutdown");
		repository.remove(savedHash);
		repository.insert(textEntry("favorite value snapshot"));
		for (const auto &entry : read(repository)) if (entry->md5 == savedHash)
			require(!entry->favorite && !entry->favoriteModified && entry->favoriteDetails == FavoriteDetails{}, "Delete left orphaned favorite metadata");
	}
}

void legacyFavoriteMigration(void)
{
	QTemporaryDir directory; const QString path = directory.filePath("legacy.db");
	auto item = textEntry("legacy favorite");
	{
		auto connection = QSqlDatabase::addDatabase("QSQLITE", "favorite-migration-fixture");
		connection.setDatabaseName(path); require(connection.open(), "Legacy fixture could not open");
		QSqlQuery query(connection);
		require(query.exec("create table favorite(md5 blob primary key, selected integer not null, modified integer not null)"), "Legacy schema failed");
		query.prepare("insert into favorite values (?, 1, 123)"); query.addBindValue(item->md5);
		require(query.exec(), "Legacy state fixture failed");
	}
	QSqlDatabase::removeDatabase("favorite-migration-fixture");
	{
		Database repository(path); repository.insert(item);
		const auto entries = read(repository);
		require(entries.size() == 1 && entries.first()->favorite && entries.first()->favoriteModified == 123 &&
			entries.first()->favoriteDetails == FavoriteDetails{}, "Upgrade lost legacy favorite state");
		repository.updateFavorite(item->md5, true, 123, {"migrated", 456, 1024, 456});
	}
	Database repository(path);
	const auto entries = read(repository);
	require(entries.size() == 1 && entries.first()->favoriteDetails.name == "migrated" &&
		entries.first()->favoriteDetails.position == 1024, "Upgraded metadata did not survive restart");
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

void sourceIconResolution(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath("icons.db");
	QHash<QByteArray, QImage> expected;
	{
		Database repository(path);
		for (int size : {16, 32, 64, 128}) {
			auto entry = textEntry(QString::number(size));
			QImage image(size, size, QImage::Format_ARGB32);
			image.fill(Qt::green); image.setPixelColor(3, 5, Qt::red);
			entry->icon = image;
			expected.insert(entry->md5, image);
			repository.insert(entry);
		}
		/* Late source lookup must keep its high-resolution pixels too. */
		auto late = textEntry("late icon");
		repository.insert(late);
		QImage icon(128, 96, QImage::Format_ARGB32);
		icon.fill(Qt::blue); icon.setPixelColor(60, 30, Qt::white);
		expected.insert(late->md5, icon);
		repository.updateIcon(late->md5, icon);
	}
	Database repository(path);
	const auto entries = read(repository);
	require(entries.size() == expected.size(), "Source icon records lost after restart");
	for (const auto &entry : entries)
		require(entry->icon == expected.value(entry->md5), "Source icon resampled during persistence");
}

void imageDuplicateRestart(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath("duplicates.db");
	const QDateTime now = QDateTime::currentDateTime();
	QByteArray newest;
	{
		Database repository(path);
		QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::cyan);
		for (int i = 0; i < 2; ++i) {
			auto entry = textEntry("");
			entry->mimeData->setUrls({QUrl::fromLocalFile(QString("/tmp/copy-%1.png").arg(i))});
			entry->mimeData->setImageData(image);
			entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
			entry->time = now.addSecs(i);
			entry->favorite = i == 0;
			entry->favoriteModified = i == 0 ? now.toMSecsSinceEpoch() : 0;
			repository.insert(entry);
			newest = entry->md5;
		}
	}
	{
		Database repository(path);
		HistoryService history(repository);
		bool loaded = false;
		QObject::connect(&history, &HistoryService::loaded, [&] { loaded = true; });
		history.load(); waitUntil([&] { return loaded; });
		require(history.entries().size() == 1 && history.entries().first()->md5 == newest && history.entries().first()->favorite,
			"Database reload retained duplicate images with different paths");
	}
	Database repository(path);
	const auto entries = read(repository);
	require(entries.size() == 1 && entries.first()->md5 == newest && entries.first()->favorite &&
		entries.first()->mimeData->urls().first().toLocalFile() == "/tmp/copy-1.png",
		"Image duplicate removal did not persist across a second restart");
}

}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	failures += runTest("queued value snapshots and shutdown", persistenceAndShutdown);
	failures += runTest("favorite snapshots, metadata updates and shutdown", favoritesPersistence);
	failures += runTest("legacy favorite metadata table upgrades without data loss", legacyFavoriteMigration);
	failures += runTest("encoded images and independent connections", imagesAndConnections);
	failures += runTest("source icon pixels survive insert, update and restart", sourceIconResolution);
	failures += runTest("image duplicates collapse durably after restart", imageDuplicateRestart);
	return failures ? 1 : 0;
}
