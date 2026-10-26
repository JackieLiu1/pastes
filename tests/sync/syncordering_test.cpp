#include "tests/sync/davfixture.h"
#include "tests/core/entryfixtures.h"
#include "storage/database.h"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFile>
#include <QJsonArray>

namespace
{
void preferences(const QString &path)
{
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
}
void load(HistoryService &history)
{
	bool ready = false;
	const auto connection =
		QObject::connect(&history, &HistoryService::loaded, &history, [&] { ready = true; });
	history.load();
	waitUntil([&] { return ready; });
	QObject::disconnect(connection);
}
QList<HistoryEntry> read(Database &repository)
{
	QList<HistoryEntry> entries;
	bool ready = false;
	const auto connection = QObject::connect(&repository, &HistoryRepository::loaded, &repository,
		[&](const QList<HistoryEntry> &value) {
			entries = value;
			ready = true;
		});
	repository.load();
	waitUntil([&] { return ready; });
	QObject::disconnect(connection);
	return entries;
}
void run(WebDavSync &service)
{
	service.synchronize();
	waitUntil([&] { return !service.busy(); }, 10000);
}
void save(WebDavSync &service, const SyncSettings &settings, const QString &password = "pass")
{
	QString error;
	require(service.save(settings, password, &error), qPrintable(error));
}
qint64 sql(const QString &path, const QString &statement)
{
	// Isolated fixture connection, owned and closed on this test thread.
	const QString name = QUuid::createUuid().toString();
	qint64 result = 0;
	{
		auto database = QSqlDatabase::addDatabase("QSQLITE", name);
		database.setDatabaseName(path);
		require(database.open(), "Cannot open fault-injection fixture");
		QSqlQuery query(database);
		require(query.exec(statement), qPrintable(query.lastError().text()));
		if (query.next()) result = query.value(0).toLongLong();
	}
	QSqlDatabase::removeDatabase(name);
	return result;
}
void databaseThenWire()
{
	QTemporaryDir directory;
	const auto captured = (QDateTime::currentSecsSinceEpoch() - 10) * 1000 + 123;
	auto original = textEntry("persist then bootstrap", QDateTime::fromMSecsSinceEpoch(captured));
	QImage icon(8, 8, QImage::Format_ARGB32);
	icon.fill(Qt::green);
	original->events.copy = {captured, 3, "source-device"};
	observeIcon(original, icon, original->events.copy);
	{
		Database repository(directory.filePath("history.db"));
		repository.insert(original);
	}
	Database repository(directory.filePath("history.db"));
	const auto entries = read(repository);
	require(entries.size() == 1, "Missing saved entry");
	const auto value = SyncContent::snapshot(*entries.first());
	QString error;
	const auto bytes = SyncContent::document(value, false, {}, &error);
	const auto record = SyncContent::parse(bytes, &error);
	require(error.isEmpty() && record.copied == captured &&
			record.events.source == original->events.source && value.time == original->time &&
			value.icon == icon,
		"Database-to-wire roundtrip changed precision or provenance");
}
void accountSwitchEmptyHistory()
{
	DavServer first, second;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	auto remote = textEntry("second account remote");
	Client seed(second.settings(), directory.path() + "/seed");
	seed.capture(remote, true);
	seed.run();
	require(seed.success, "Seed failed");
	Database repository(directory.filePath("history.db"));
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	history.record(textEntry("first account local"));
	save(service, first.settings());
	run(service);
	history.remove(history.entries().first()->id);
	save(service, second.settings());
	run(service);
	run(service);
	require(history.entries().size() == 1 && history.entries().first()->md5 == remote->md5 &&
			read(repository).size() == 1,
		"Account switch rejected unrelated remote history");
}
void localCacheWithoutProjection()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const QString database = directory.filePath("history.db");
	{
		Database repository(database);
		HistoryService history(repository);
		WebDavSync service(history, secrets, directory.path() + "/cache");
		load(history);
		history.record(textEntry("durable local cache recovery"));
		save(service, server.settings());
		run(service);
		require(!server.files.isEmpty(), "Seed upload failed");
	}
	require(QFile::remove(database), "Cannot simulate missing history database");
	server.files.clear(); // Recovery must work from the retained cache as well.
	Database repository(database);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	run(service);
	run(service);
	require(
		history.entries().size() == 1 && read(repository).size() == 1 && !server.files.isEmpty(),
		"Locally originated cache was not durably restored and republished");
}
void offlineCopyAfterObservedDelete()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	auto original = textEntry("offline explicit recopy", QDateTime::currentDateTime().addSecs(-10));
	const qint64 ahead = QDateTime::currentMSecsSinceEpoch() + 60000;
	const QJsonObject marker{{"version", 1},
		{"key", SyncContent::key(SyncContent::snapshot(*original))}, {"modified", ahead},
		{"deleted", true}};
	const auto bytes = SyncContent::bytes(marker);
	server.files["/Pastes/v1/events/" + SyncContent::digest(bytes) + ".json"] = bytes;
	Database repository(directory.filePath("history.db"));
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	history.record(original);
	save(service, server.settings());
	run(service);
	require(history.entries().isEmpty(), "Remote deletion setup failed");
	auto disabled = server.settings();
	disabled.enabled = false;
	save(service, disabled, {});
	history.record(textEntry("offline explicit recopy"));
	const auto copy = history.entries().first()->events.copy;
	require(copy > EventVersion::legacy(ahead),
		"Local operation did not advance the observed remote clock");
	save(service, server.settings(), {});
	run(service);
	run(service);
	require(history.entries().size() == 1 && read(repository).size() == 1,
		"Old deletion removed a causally later offline copy");
}
void lateIconAfterDeletion()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	Database firstDb(directory.filePath("first.db"));
	HistoryService first(firstDb);
	WebDavSync firstSync(first, secrets, directory.path() + "/first-cache");
	load(first);
	first.record(textEntry("late icon after deletion"), 10);
	save(firstSync, server.settings());
	run(firstSync);
	Database secondDb(directory.filePath("second.db"));
	HistoryService second(secondDb);
	WebDavSync secondSync(second, secrets, directory.path() + "/second-cache");
	load(second);
	run(secondSync);
	require(second.entries().size() == 1, "Second device did not receive the copy");
	second.remove(second.entries().first()->id);
	run(secondSync);
	QImage icon(8, 8, QImage::Format_ARGB32);
	icon.fill(Qt::blue);
	first.setSourceIcon(10, icon);
	first.setFavorite(first.entries().first()->id, true);
	run(firstSync);
	run(secondSync);
	Client fresh(server.settings(), directory.path() + "/observer");
	fresh.run();
	require(first.entries().isEmpty() && second.entries().isEmpty() && read(firstDb).isEmpty() &&
			fresh.success && fresh.deliveries.size() == 1 && fresh.deliveries.first().deleted,
		"Late icon metadata resurrected deleted content");
}
void databaseFailureAcknowledged()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	Client seed(server.settings(), directory.path() + "/seed");
	seed.capture(textEntry("failed remote persistence"), true);
	seed.run();
	require(seed.success, "Seed failed");
	const auto path = directory.filePath("history.db");
	Database repository(path);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	sql(path,
		"create trigger reject_item before insert on item begin select raise(ABORT,'injected "
		"write failure'); end");
	int failures = 0;
	QObject::connect(&repository, &HistoryRepository::commitFailed, &repository,
		[&](quint64, const QString &) { ++failures; });
	save(service, server.settings());
	run(service);
	require(failures > 0 && !service.lastSuccess().isValid() &&
			sql(path, "select count(*) from sync_applied") == 0 &&
			sql(path, "select count(*) from sync_events") == 0,
		"Failed SQL transaction was acknowledged or journaled as committed");
	const int previous = failures;
	run(service);
	require(failures > previous && !service.lastSuccess().isValid(),
		"Failed application was not retried");
	sql(path, "drop trigger reject_item");
	run(service);
	run(service);
	require(service.lastSuccess().isValid() && read(repository).size() == 1 &&
			sql(path, "select count(*) from sync_applied") > 0,
		"Successful retry did not commit history before acknowledgement");
}
void freshLocalIconAfterAheadClock()
{
	QTemporaryDir directory;
	const auto path = directory.filePath("history.db");
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	EventVersion old;
	{
		Database repository(path);
		HistoryService history(repository);
		load(history);
		auto remote = textEntry("known ahead icon", QDateTime::fromMSecsSinceEpoch(now + 60000));
		remote->events.copy = {now + 60000, 0, "remote"};
		QImage icon(8, 8, QImage::Format_ARGB32);
		icon.fill(Qt::blue);
		observeIcon(remote, icon, remote->events.copy);
		history.mergeSynced(remote, {});
		old = remote->events.source;
	}
	Database repository(path);
	HistoryService history(repository);
	load(history);
	auto fresh = textEntry("known ahead icon", QDateTime::fromMSecsSinceEpoch(now));
	QImage icon(8, 8, QImage::Format_ARGB32);
	icon.fill(Qt::green);
	fresh->icon = icon;
	history.record(fresh);
	require(history.entries().first()->icon == icon && fresh->events.source > old &&
			read(repository).first()->icon == icon,
		"Older wall-clock-ahead icon overrode a later native observation after restart");
}
void localCacheWriteRetry(bool indexFailure)
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const auto database = directory.filePath("history.db");
	Database repository(database);
	HistoryService history(repository);
	const auto cache = directory.path() + "/cache";
	WebDavSync service(history, secrets, cache);
	load(history);
	history.record(textEntry("saved before cache failure"));
	const auto settings = server.settings();
	save(service, settings);
	run(service);
	require(service.lastSuccess().isValid() && server.files.size() == 1,
		"Initial cache write fixture did not synchronize");
	const auto previousSuccess = service.lastSuccess();
	const auto account = SyncContent::digest(
		settings.url.toEncoded() + QByteArray(1, '\0') + settings.username.toUtf8());
	QDir accountDirectory(cache + '/' + account);
	const QString target = indexFailure ? QStringLiteral("index.json") : QStringLiteral("events");
	const QString held = target + ".held";
	require(accountDirectory.rename(target, held), "Could not preserve the cache failure fixture");
	if (indexFailure)
		require(accountDirectory.mkdir(target), "Could not block the temporary index target");
	else {
		QFile blocker(accountDirectory.filePath(target));
		require(blocker.open(QIODevice::WriteOnly), "Could not block the temporary record directory");
	}
	history.record(textEntry("first record during cache failure"));
	history.record(textEntry("queued behind the failed cache write"));
	waitUntil([&] { return !history.hasInFlightWrites(); });
	run(service);
	require(service.lastSuccess() == previousSuccess && server.files.size() == 1 &&
		service.status().contains(indexFailure ? "Could not save the local sync index."
			: "Could not save a sync record."),
		"A cache write failure was reported as a successful synchronization");
	const bool removed = indexFailure ? accountDirectory.rmdir(target)
		: QFile::remove(accountDirectory.filePath(target));
	require(removed && accountDirectory.rename(held, target),
		"Could not release the temporary cache write failure");
	run(service);
	require(service.lastSuccess() > previousSuccess && service.status() == "Up to date." &&
		read(repository).size() == 3 && server.files.size() == 3,
		"Cache write retry retained its old error or skipped a committed SQL record");
	Client observer(settings, directory.path() + "/observer");
	observer.run();
	require(observer.success && observer.deliveries.size() == 3,
		"A fresh device could not recover every record after cache write retry");
}
void atomicReplacementAndOfflineOutbox()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const auto path = directory.filePath("history.db");
	QImage old(8, 8, QImage::Format_ARGB32);
	old.fill(Qt::blue);
	{
		Database repository(path);
		HistoryService history(repository);
		load(history);
		auto item = textEntry("atomic replacement");
		item->icon = old;
		history.record(item);
		require(read(repository).size() == 1, "Initial SQL commit failed");
		sql(path,
			"create trigger reject_item before insert on item begin select "
			"raise(ABORT,'replace failure'); end");
		QImage icon(8, 8, QImage::Format_ARGB32);
		icon.fill(Qt::green);
		auto newer = textEntry("atomic replacement");
		newer->icon = icon;
		history.record(newer);
		waitUntil([&] { return history.hasPersistenceFailures(); });
		require(read(repository).first()->icon == old &&
				sql(path, "select count(*) from sync_events") == 1,
			"Failed replacement lost its old row or published the failed new state");
		sql(path, "drop trigger reject_item");
		history.retryPersistence();
		waitUntil([&] { return !history.hasPersistenceFailures(); });
		require(read(repository).first()->icon == icon, "Retry failed to replace atomically");
	}
	// No sync service existed when those operations were committed.
	Database repository(path);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/fresh-cache");
	load(history);
	save(service, server.settings());
	run(service);
	Client observer(server.settings(), directory.path() + "/observer");
	observer.run();
	require(observer.success && observer.deliveries.size() == 1 &&
			observer.deliveries.first().content.icon.pixelColor(0, 0) == QColor(Qt::green),
		"Committed outbox did not survive restart without a worker cache");
}
void disabledFavoriteRestart()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const auto path = directory.filePath("history.db");
	{
		Database repository(path);
		HistoryService history(repository);
		WebDavSync service(history, secrets, directory.path() + "/cache");
		load(history);
		history.record(textEntry("offline favorite"));
		save(service, server.settings());
		run(service);
		auto disabled = server.settings();
		disabled.enabled = false;
		save(service, disabled, {});
		const auto item = history.entries().first();
		history.setFavorite(item->id, true);
		history.setFavoriteName(item->id, "offline name");
		require(read(repository).first()->favoriteDetails.name == "offline name",
			"Offline favorite did not reach SQL");
	}
	Database repository(path);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	save(service, server.settings(), {});
	run(service);
	Client observer(server.settings(), directory.path() + "/observer");
	observer.run();
	require(observer.success && observer.deliveries.size() == 1 &&
			observer.deliveries.first().content.favorite &&
			observer.deliveries.first().content.favoriteDetails.name == "offline name",
		"Disabled favorite journal did not survive restart");
}
void favoriteDeleteUndoAndRecopy()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	Database firstDb(directory.filePath("first.db"));
	HistoryService first(firstDb);
	WebDavSync firstSync(first, secrets, directory.path() + "/first-cache");
	load(first);
	first.record(textEntry("deleted favorite"));
	const auto item = first.entries().first();
	first.setFavorite(item->id, true);
	first.setFavoriteName(item->id, "saved name");
	save(firstSync, server.settings());
	run(firstSync);
	Database secondDb(directory.filePath("second.db"));
	HistoryService second(secondDb);
	WebDavSync secondSync(second, secrets, directory.path() + "/second-cache");
	load(second);
	run(secondSync);
	first.remove(item->id);
	run(firstSync);
	run(firstSync);
	run(secondSync);
	require(second.entries().isEmpty(), "Favorite deletion did not converge");
	first.undo();
	run(firstSync);
	run(secondSync);
	require(second.entries().size() == 1 && second.entries().first()->favorite &&
			second.entries().first()->favoriteDetails.name == "saved name",
		"Undo did not restore favorite metadata after deletion compaction");
	first.remove(first.entries().first()->id);
	run(firstSync);
	run(firstSync);
	run(secondSync);
	first.record(textEntry("deleted favorite"));
	run(firstSync);
	run(secondSync);
	require(second.entries().size() == 1 && !second.entries().first()->favorite &&
			second.entries().first()->favoriteDetails.name.isEmpty(),
		"A new copy inherited metadata cleared by deletion");
}
void legacyUndoAcrossLayers()
{
	for (int version : {1, 2, 3}) for (bool cachedDelete : {false, true}) {
		DavServer server;
		QTemporaryDir directory;
		preferences(directory.path() + "/preferences");
		MemorySecrets secrets;
		const auto settings = server.settings();
		const auto account = SyncContent::digest(
			settings.url.toEncoded() + QByteArray(1, '\0') + settings.username.toUtf8());
		const auto cache = directory.path() + "/cache";
		const auto path = directory.filePath("history.db");
		const qint64 copied = QDateTime::currentMSecsSinceEpoch() - 23LL * 86400000 + 123;
		const qint64 erased = copied + 86400000;
		auto original = textEntry("legacy restored history", QDateTime::fromMSecsSinceEpoch(copied));
		if (version == 2) {
			QImage image(24, 16, QImage::Format_ARGB32);
			image.fill(Qt::green);
			original->mimeData = std::make_unique<QMimeData>();
			original->mimeData->setImageData(image);
			original->md5 = ClipboardContent::fingerprint(*original->mimeData);
		}
		QString error;
		const auto portable = SyncContent::snapshot(*original);
		const auto payload = SyncContent::encode(portable, &error);
		const auto key = SyncContent::key(SyncContent::decode(payload, &error));
		require(error.isEmpty(), qPrintable(error));
		QJsonObject live{{"version", version}, {"key", key}, {"modified", copied},
			{"copied", copied}, {"deleted", false}, {"content", payload}, {"favorite", false}};
		if (version == 3) {
			live["favoriteName"] = "";
			live["favoriteNameModified"] = 0;
			live["favoritePosition"] = 0;
			live["favoritePositionModified"] = 0;
		}
		auto marker = live;
		marker["modified"] = erased;
		marker["deleted"] = true;
		marker.remove("copied");
		marker.remove("content");
		auto restored = live;
		restored["modified"] = erased + 1103;
		for (const auto &object : {live, restored}) {
			const auto bytes = SyncContent::bytes(object);
			server.files["/Pastes/v1/events/" + SyncContent::digest(bytes) + ".json"] = bytes;
		}
		const auto markerBytes = SyncContent::bytes(marker);
		const auto markerId = SyncContent::digest(markerBytes);
		if (cachedDelete) {
			const auto accountCache = cache + "/" + account;
			require(QDir().mkpath(accountCache + "/events"), "Cannot create legacy cache fixture");
			QFile record(accountCache + "/events/" + markerId + ".json");
			require(record.open(QIODevice::WriteOnly) && record.write(markerBytes) == markerBytes.size(),
				"Cannot seed cached legacy deletion");
			record.close();
			QFile index(accountCache + "/index.json");
			const auto bytes = SyncContent::bytes({{"local", QJsonArray{markerId}}});
			require(index.open(QIODevice::WriteOnly) && index.write(bytes) == bytes.size(),
				"Cannot seed legacy cache index");
		} else
			server.files["/Pastes/v1/events/" + markerId + ".json"] = markerBytes;
		EventState migrated;
		{
			Database repository(path);
			HistoryService history(repository);
			WebDavSync service(history, secrets, cache);
			load(history);
			// Reproduce a previous upgrade that already committed the wrong tombstone.
			auto hidden = portable;
			hidden.events.copy = EventVersion::legacy(copied);
			hidden.events.erase = EventVersion::legacy(erased);
			hidden.events.erasedAt = erased;
			history.applySynced({}, {}, hidden.events, key, account, "old-migration", hidden);
			require(read(repository).isEmpty() && sql(path, "select count(*) from sync_events") == 1,
				"Cannot seed the previously migrated deletion");
			save(service, settings);
			run(service);
			run(service);
			require(service.lastSuccess().isValid() && history.entries().size() == 1,
				"Legacy undo was removed while merging WebDAV, cache and SQL state");
			const auto entry = history.entries().first();
			migrated = entry->events;
			const auto stored = read(repository);
			require(stored.size() == 1 && entry->time == original->time &&
					stored.first()->time == original->time && !migrated.deleted() &&
					migrated.copy == EventVersion::legacy(copied) &&
					stored.first()->events.json() == migrated.json(),
				"Migrated presence or original copy milliseconds did not reach SQL");
			const auto content = SyncContent::decode(
				SyncContent::encode(SyncContent::snapshot(*stored.first()), &error), &error);
			require(error.isEmpty() && SyncContent::key(content) == key,
				"Legacy migration changed text or image content");
		}
		require(QDir(cache).removeRecursively(), "Cannot remove temporary transport cache");
		Database repository(path);
		HistoryService history(repository);
		WebDavSync service(history, secrets, cache);
		load(history);
		require(history.entries().size() == 1 &&
				history.entries().first()->events.json() == migrated.json(),
			"SQL restart lost the migrated presence");
		run(service);
		run(service);
		require(service.lastSuccess().isValid() && history.entries().size() == 1 &&
				history.entries().first()->time == original->time,
			"Restart or legacy replay removed restored history");
		history.remove(history.entries().first()->id);
		run(service);
		run(service);
		Client observer(settings, directory.path() + "/observer");
		observer.run();
		require(observer.success && observer.deliveries.size() == 1 &&
				observer.deliveries.first().deleted && history.entries().isEmpty() &&
				read(repository).isEmpty(),
			"Legacy replay revived a later native deletion");
	}
}
void staleSnapshotsRespectDurableProjection()
{
	QTemporaryDir directory;
	Database repository(directory.filePath("history.db"));
	HistoryService history(repository);
	load(history);
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	QImage pixels(24, 16, QImage::Format_ARGB32);
	pixels.fill(Qt::blue);
	auto imageEntry = [&](const QString &path, EventVersion version) {
		auto entry = HistoryEntry::create();
		entry->mimeData = std::make_unique<QMimeData>();
		entry->mimeData->setImageData(pixels);
		entry->mimeData->setUrls({QUrl::fromLocalFile(path)});
		entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
		entry->time = QDateTime::fromMSecsSinceEpoch(now);
		entry->events.copy = version;
		return entry;
	};
	auto delayed = imageEntry("/tmp/old-copy.png", {now, 0, "capture"});
	auto remote = imageEntry("/tmp/new-copy.png", {now, 1, "remote"});
	history.mergeSynced(remote, {});
	require(read(repository).size() == 1, "Initial projection missing");
	history.record(cloneEntry(*delayed));
	require(
		history.entries().first() == remote, "A delayed capture rewound a newer in-memory copy");
	// Exercise the storage boundary directly with a differently normalized MD5.
	repository.save(delayed, {{remote->md5}, false, true, {}, {}, {}});
	auto stored = read(repository);
	require(stored.size() == 1 && stored.first()->events.copy == remote->events.copy &&
			stored.first()->mimeData->urls() == remote->mimeData->urls(),
		"SQL replaced newer native formats with a stale snapshot");
	QString error;
	const auto portable = SyncContent::snapshot(*remote);
	const auto key =
		SyncContent::key(SyncContent::decode(SyncContent::encode(portable, &error), &error));
	auto tomb = portable;
	tomb.events.erase = {now, 2, "deleter"};
	tomb.events.erasedAt = now;
	tomb.events.favorite = tomb.events.name = tomb.events.position = tomb.events.erase;
	tomb.favorite = false;
	tomb.favoriteDetails = {};
	history.applySynced({}, {remote->md5}, tomb.events, key, "account", "delete-record", tomb);
	require(read(repository).isEmpty(), "Delete did not reach SQL");
	// The old read completes after the newer delete and has another local MD5.
	history.record(delayed, 77);
	waitUntil([&] { return history.entries().isEmpty(); });
	require(read(repository).isEmpty(), "A stale deferred capture revived durable deletion");
	QImage icon(8, 8, QImage::Format_ARGB32);
	icon.fill(Qt::green);
	history.setSourceIcon(77, icon);
	require(history.entries().isEmpty(), "A callback revived a rejected deferred snapshot");
}
void syncWaitsForConcurrentLocalCommit()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const auto path = directory.filePath("history.db");
	Database repository(path);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	history.record(textEntry("initial committed item"));
	save(service, server.settings());
	run(service);
	const QString name = QUuid::createUuid().toString();
	{
		auto locker = QSqlDatabase::addDatabase("QSQLITE", name);
		locker.setDatabaseName(path);
		require(locker.open(), "Cannot open temporary SQL lock fixture");
		QSqlQuery lock(locker);
		bool armed = true, released = false;
		server.onRequest = [&](const QByteArray &method) {
			if (!armed || method != "PROPFIND") return;
			armed = false;
			require(lock.exec("begin immediate"), "Cannot hold SQL write lock");
			history.record(textEntry("copy while network sync is finishing"));
			QTimer::singleShot(150, &service, [&] {
				require(lock.exec("commit"), "Cannot release SQL fixture lock");
				released = true;
			});
		};
		run(service);
		server.onRequest = {};
		require(released && service.lastSuccess().isValid() && server.files.size() == 2 &&
				read(repository).size() == 2,
			"Sync reported completion before a concurrent local commit was saved and uploaded");
	}
	QSqlDatabase::removeDatabase(name);
}
void journalRestoresLostCacheAndServer()
{
	DavServer server;
	QTemporaryDir directory;
	preferences(directory.path() + "/preferences");
	MemorySecrets secrets;
	const auto path = directory.filePath("history.db");
	EventVersion copy;
	{
		Database repository(path);
		HistoryService history(repository);
		WebDavSync service(history, secrets, directory.path() + "/cache");
		load(history);
		for (int i = 0; i < 8; ++i)
			history.record(textEntry("compacted durable state"));
		save(service, server.settings());
		run(service);
		copy = history.entries().first()->events.copy;
		require(sql(path, "select count(*) from sync_events") == 1,
			"Superseded local states were not compacted");
	}
	require(QDir(directory.path() + "/cache").removeRecursively(),
		"Cannot remove temporary transport cache");
	server.files.clear();
	Database repository(path);
	HistoryService history(repository);
	WebDavSync service(history, secrets, directory.path() + "/cache");
	load(history);
	run(service);
	require(service.lastSuccess().isValid() && server.files.size() == 1 &&
			history.entries().first()->events.copy == copy,
		"Already-published SQL state could not restore a lost cache and empty server");
}
void causalMergeAndClockRollback()
{
	EventClock clock;
	clock.last = {QDateTime::currentMSecsSinceEpoch() + 60000, 9, "remote"};
	const auto first = clock.tick(QDateTime::currentMSecsSinceEpoch() - 60000);
	const auto second = clock.tick(QDateTime::currentMSecsSinceEpoch() - 120000);
	require(second > first && first.logical == 10, "Wall clock rollback reversed event ordering");
	auto original = SyncContent::snapshot(*textEntry("permutation convergence"));
	original.events.copy = {first.physical, 0, "first"};
	QImage blue(8, 8, QImage::Format_ARGB32);
	blue.fill(Qt::blue);
	original.icon = blue;
	original.events.source = original.events.copy;
	auto newer = original;
	newer.events.copy = first;
	newer.events.source = first;
	QImage green(8, 8, QImage::Format_ARGB32);
	green.fill(Qt::green);
	newer.icon = green;
	auto echo = original;
	echo.events.copy = second;
	echo.time = echo.time.addSecs(2);
	const auto a = SyncContent::merge(SyncContent::merge(original, newer), echo);
	const auto b = SyncContent::merge(echo, SyncContent::merge(newer, original));
	require(a.events.copy == second && a.events.source == first && a.icon == green &&
			b.icon == green && b.events.copy == a.events.copy,
		"Delivery order changed the selected copy or observed source");
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	app.setOrganizationName("PastesTests");
	app.setApplicationName("SyncOrderingContracts");
	int failures = 0;
	failures += runTest("database to wire preserves milliseconds and provenance", databaseThenWire);
	failures += runTest("empty-history account switch", accountSwitchEmptyHistory);
	failures += runTest("local cache recovers missing SQL history", localCacheWithoutProjection);
	failures += runTest("offline copy follows observed deletion", offlineCopyAfterObservedDelete);
	failures += runTest("late icon does not resurrect deletion", lateIconAfterDeletion);
	failures +=
		runTest("SQL failure blocks acknowledgement and retries", databaseFailureAcknowledged);
	failures += runTest(
		"fresh native icon follows ahead clock after restart", freshLocalIconAfterAheadClock);
	failures +=
		runTest("atomic replacement and durable offline outbox", atomicReplacementAndOfflineOutbox);
	failures += runTest("local index write failure recovers without restart", [] { localCacheWriteRetry(true); });
	failures += runTest("local record write failure replays committed SQL state", [] { localCacheWriteRetry(false); });
	failures += runTest("disabled favorite changes survive SQL restart", disabledFavoriteRestart);
	failures +=
		runTest("favorite deletion, undo and new copy converge", favoriteDeleteUndoAndRecopy);
	failures += runTest("legacy undo survives WebDAV, cache and SQL restart", legacyUndoAcrossLayers);
	failures += runTest(
		"stale captures respect the durable projection", staleSnapshotsRespectDurableProjection);
	failures +=
		runTest("sync waits for a concurrent local commit", syncWaitsForConcurrentLocalCommit);
	failures +=
		runTest("SQL journal restores cache and remote state", journalRestoresLostCacheAndServer);
	failures += runTest("causal convergence and clock rollback", causalMergeAndClockRollback);
	return failures ? 1 : 0;
}
