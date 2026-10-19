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
} // namespace
int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return runTest("persistent causal versions", versions) +
		runTest("field merge permutations and replay", permutations) +
		runTest("source completion and deletion are independent", callbackAndDeletion);
}
