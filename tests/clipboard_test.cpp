#include "tests/testsupport.h"
#include "application/clipboardcontroller.h"
#include <QClipboard>
#include <QGuiApplication>
#include <utility>

class TestFeed final : public ClipboardFeed
{
public:
	int settleInterval(void) const override { return 0; }
	void capture(quint64 request) override { lastRequest = request; }
	bool synchronize(void) override
	{
		if (!onSynchronize) return false;
		return onSynchronize();
	}
	QImage snapshotIcon(void) override { return icon; }
	void notify(void) { emit clipboardChanged(); }
	QImage icon;
	quint64 lastRequest = 0;
	std::function<bool()> onSynchronize;
};

void clipboardLifecycle(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	TestFeed feed;
	feed.icon = QImage(2, 2, QImage::Format_ARGB32); feed.icon.fill(Qt::green);
	QClipboard &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);
	clipboard.setText("first"); feed.notify();
	waitUntil([&] { return history.entries().size() == 1; });
	require(history.entries().first()->icon == feed.icon, "Native source icon was lost");
	controller.setRecordingEnabled(false);
	clipboard.setText("paused"); feed.notify();
	controller.flushPending();
	require(history.entries().size() == 1, "Paused clipboard change was recorded");
	feed.onSynchronize = [&] { feed.notify(); return true; };
	controller.setRecordingEnabled(true);
	feed.onSynchronize = {};
	controller.flushPending();
	require(history.entries().size() == 1, "Resuming recorded the paused clipboard");
	clipboard.setText("second"); feed.notify();
	waitUntil([&] { return history.entries().size() == 2; });
	const auto original = history.entries().last();
	require(controller.copy(*original), "Internal copy failed");
	waitUntil([&] { return history.entries().first()->mimeData->text() == "first"; });
	require(history.entries().size() == 2, "Internal copy failed to deduplicate");
	require(history.entries().first()->icon == original->icon, "Internal copy changed original source");

	/* Invalidate a snapshot after MIME duplication, as promised native
	 * flavors can deliver another change during the capture operation. */
	int syncCalls = 0;
	feed.onSynchronize = [&] {
		if (++syncCalls != 2) return false;
		clipboard.setText("newest"); feed.notify();
		return true;
	};
	clipboard.setText("superseded"); feed.notify();
	waitUntil([&] { return history.entries().first()->mimeData->text() == "newest"; });
	for (const auto &entry : history.entries())
		require(entry->mimeData->text() != "superseded", "Invalidated snapshot entered history");
}

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	return runTest("clipboard pause, copy and native synchronization", clipboardLifecycle);
}
