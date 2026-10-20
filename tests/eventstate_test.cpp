#include "tests/testsupport.h"
#include "core/synccontent.h"
#include <QCoreApplication>
#include <algorithm>
#include <array>

namespace
{
void versions(void)
{
	EventClock clock;
	clock.device = "local";
	clock.observe({1000, 9, "remote"});
	const auto first = clock.tick(900), second = clock.tick(800);
	require(first == EventVersion{1000, 10, "local"} && second > first,
		"Known causality or clock rollback was lost");
	EventClock restored;
	restored.device = clock.device;
	restored.observe(second);
	require(restored.tick(700) > second, "Restored logical clock reused an event identity");
	clock.last = {1000, EventVersion::maxLogical, "remote"};
	require(clock.tick(900) == EventVersion{1001, 0, "local"},
		"Counter overflow did not advance the physical component");
}
void permutations(void)
{
	std::array<SyncContent::Snapshot, 4> values;
	for (auto &value : values) {
		value =
			SyncContent::snapshot(*textEntry("same content", QDateTime::fromMSecsSinceEpoch(1000)));
		value.events.copy = {1000, 0, "original"};
	}
	QImage old(8, 8, QImage::Format_ARGB32);
	old.fill(Qt::blue);
	values[0].icon = old;
	values[0].events.source = values[0].events.copy;
	values[1] = values[0];
	values[1].events.copy = {2000, 0, "new-source"};
	values[1].events.source = values[1].events.copy;
	QImage fresh(8, 8, QImage::Format_ARGB32);
	fresh.fill(Qt::green);
	values[1].icon = fresh;
	values[2] = values[0];
	values[2].events.copy = {3000, 0, "inherited"};
	values[2].time = QDateTime::fromMSecsSinceEpoch(3000);
	values[3].favorite = true;
	values[3].favoriteModified = 4000;
	values[3].events.favorite = {4000, 0, "star"};
	values[3].favoriteDetails = {"named", 5000, 2048, 6000};
	values[3].events.name = {5000, 0, "name"};
	values[3].events.position = {6000, 0, "position"};
	std::array<int, 4> order{0, 1, 2, 3};
	QByteArray expected;
	do {
		auto merged = values[order[0]];
		for (int i = 1; i < 4; ++i)
			merged = SyncContent::merge(merged, values[order[i]]);
		QString error;
		const auto bytes = SyncContent::document(merged, false, {}, &error);
		require(error.isEmpty() && merged.icon == fresh &&
				merged.events.copy == values[2].events.copy && merged.favorite &&
				merged.favoriteDetails.name == "named" && merged.favoriteDetails.position == 2048,
			"A field lost its independent version");
		if (expected.isEmpty()) expected = bytes;
		require(bytes == expected, "Delivery permutation changed the merged document");
		const auto repeated = SyncContent::merge(merged, merged);
		require(SyncContent::document(repeated, false, {}, &error) == expected,
			"Repeated delivery manufactured a new state");
	} while (std::next_permutation(order.begin(), order.end()));
}
void callbackAndDeletion(void)
{
	auto initial = SyncContent::snapshot(*textEntry("source result", QDateTime::currentDateTime()));
	initial.events.copy = {initial.time.toMSecsSinceEpoch(), 0, "copy"};
	initial.events.source = initial.events.copy;
	initial.icon = QImage(8, 8, QImage::Format_ARGB32);
	initial.icon.fill(Qt::blue);
	auto final = initial;
	final.events.sourceRevision = 1;
	final.icon.fill(Qt::green);
	auto deleted = initial.events;
	deleted.erase = nextVersion(deleted.copy);
	deleted.erasedAt = initial.time.toMSecsSinceEpoch();
	const auto result = SyncContent::merge(initial, final);
	require(result.icon == final.icon && result.events.copy == initial.events.copy &&
			result.events.source == initial.events.source,
		"Late source completion changed the copy or failed to replace its initial snapshot");
	auto events = result.events;
	events.observe(deleted);
	require(events.deleted(), "A source callback restored a deleted item");
	auto hidden = initial;
	hidden.events = deleted;
	hidden.favorite = true;
	hidden.favoriteModified = initial.time.toMSecsSinceEpoch();
	hidden.events.favorite = nextVersion(hidden.events.erase);
	QString error;
	const auto marker = SyncContent::parse(SyncContent::document(hidden, true, "", &error), &error);
	require(error.isEmpty() && marker.deleted && marker.favorite &&
			marker.events.favorite == hidden.events.favorite &&
			!marker.document.contains("content"),
		"A metadata operation was lost or revived deleted content");
	auto later = initial;
	later.events.copy = nextVersion(deleted.erase);
	later.events.source = initial.events.source;
	auto tomb = SyncContent::Snapshot();
	tomb.events = deleted;
	tomb.events.source = nextVersion(initial.events.source);
	const auto kept = SyncContent::merge(later, tomb);
	require(!kept.events.deleted() && kept.icon == initial.icon &&
			kept.events.source == initial.events.source,
		"A payload-free tombstone falsely attributed the retained icon to another observation");
}
void legacyPresenceMigration(void)
{
	const qint64 copied = 1700000000123;
	const auto value = SyncContent::snapshot(
		*textEntry("legacy undo", QDateTime::fromMSecsSinceEpoch(copied)));
	QString error;
	const auto payload = SyncContent::encode(value, &error);
	for (int version : {1, 2, 3}) {
		QJsonObject initial{{"version", version}, {"key", SyncContent::key(value)},
			{"modified", copied}, {"copied", copied}, {"deleted", false},
			{"content", payload}, {"favorite", false}};
		if (version == 3) {
			initial["favoriteName"] = "";
			initial["favoriteNameModified"] = 0;
			initial["favoritePosition"] = 0;
			initial["favoritePositionModified"] = 0;
		}
		auto recopy = initial;
		recopy["modified"] = copied + 1000;
		recopy["copied"] = copied + 1000;
		auto deleted = initial;
		deleted["modified"] = copied + 2000;
		deleted["deleted"] = true;
		deleted.remove("content");
		deleted.remove("copied");
		auto restored = recopy;
		restored["modified"] = copied + 3103;
		// Old metadata can carry an older copy time than another live record.
		auto metadata = initial;
		metadata["modified"] = copied + 4000;
		QList<SyncContent::Record> records;
		for (const auto &object : {initial, recopy, deleted, restored, metadata}) {
			const auto bytes = SyncContent::bytes(object);
			const auto record = SyncContent::parse(bytes, &error);
			const auto inspected = SyncContent::inspect(bytes, &error);
			require(error.isEmpty() && record.events.json() == inspected.events.json(),
				"Legacy parsing and SQL inspection disagree");
			records.append(record);
		}
		auto reduce = [&](const QList<SyncContent::Record> &inputs) {
			const auto bytes = SyncContent::reduce(inputs, {}, &error);
			const auto result = SyncContent::parse(bytes, &error);
			require(error.isEmpty(), qPrintable(error));
			return result;
		};
		std::array<int, 5> order{0, 1, 2, 3, 4};
		QJsonObject expected;
		do {
			QList<SyncContent::Record> reordered;
			for (int index : order) reordered.append(records[index]);
			const auto result = reduce(reordered);
			require(!result.deleted && result.copied == copied + 1000 &&
					result.events.copy == EventVersion::legacy(copied + 1000) &&
					!result.events.source.valid(),
				"Legacy presence migration lost an undo or rewrote copy/source identity");
			if (expected.isEmpty()) expected = result.document;
			require(result.document == expected, "Legacy migration depends on delivery order");
			auto folded = reordered.takeFirst();
			for (const auto &record : reordered) folded = reduce({folded, record});
			require(folded.document == expected && reduce({result, result}).document == expected,
				"Legacy migration did not survive incremental compaction or replay");
		} while (std::next_permutation(order.begin(), order.end()));
		const auto migrated = reduce(records);
		for (qint64 invalid : {qint64(-1), EventVersion::maxPhysical + 1}) {
			auto document = migrated.document;
			auto events = document.value("events").toObject();
			events["legacyPresence"] = invalid;
			document["events"] = events;
			SyncContent::parse(SyncContent::bytes(document), &error);
			require(!error.isEmpty(), "Invalid migrated presence passed validation");
			error.clear();
		}
		auto tied = restored;
		tied["modified"] = deleted.value("modified");
		const auto tiedRecord = SyncContent::parse(SyncContent::bytes(tied), &error);
		require(error.isEmpty() && reduce({records[2], tiedRecord}).deleted,
			"A legacy live record at the deletion time revived the item");
		require(!SyncContent::expired(migrated, copied + 30LL * 86400000) &&
				SyncContent::expired(migrated, copied + 1000 + 30LL * 86400000),
			"Legacy presence updates changed the content retention time");
		// Typed metadata retains its old presence even when modified is newer.
		auto typed = value;
		typed.events = records.first().events;
		typed.events.favorite = {copied + 5000, 0, "favorite"};
		typed.favoriteModified = copied + 5000;
		typed.favorite = true;
		const auto typedRecord = SyncContent::parse(
			SyncContent::document(typed, false, {}, &error), &error);
		require(reduce({records[2], typedRecord}).deleted,
			"Typed metadata was incorrectly treated as a legacy restore");
		EventClock clock;
		clock.observe(migrated.events.latest());
		auto removed = value;
		removed.events = migrated.events;
		removed.events.erase = clock.tick(copied);
		removed.events.erasedAt = copied + 5000;
		const auto marker = SyncContent::parse(
			SyncContent::document(removed, true, migrated.key, &error), &error);
		require(error.isEmpty() && marker.deleted && reduce({migrated, marker}).deleted,
			"A native delete did not advance past the migrated presence");
		require(reduce({marker, records[3], records[4]}).deleted,
			"Replayed legacy records revived a newer native deletion");
		auto undo = removed;
		undo.events.copy = clock.tick(copied - 1000);
		const auto undoRecord = SyncContent::parse(
			SyncContent::document(undo, false, {}, &error), &error);
		const auto recovered = reduce({marker, undoRecord, records[4]});
		require(error.isEmpty() && !recovered.deleted && recovered.copied == copied &&
				recovered.events.copy == undo.events.copy,
			"Native undo after migration changed the retained original copy time");
	}
}
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return runTest("persistent causal versions", versions) +
		runTest("field merge permutations and replay", permutations) +
		runTest("source completion and deletion are independent", callbackAndDeletion) +
		runTest("legacy presence migration preserves copy identity", legacyPresenceMigration);
}
