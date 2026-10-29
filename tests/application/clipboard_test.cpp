#include "tests/application/memoryrepository.h"
#include "application/clipboardcontroller.h"
#include "core/sourceicon.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QPointer>
#include <QUrl>
#include <utility>

class TestFeed final : public ClipboardFeed
{
public:
	int settleInterval(void) const override { return interval; }
	void capture(quint64 request) override { lastRequest = request; }
	bool synchronize(void) override
	{
		if (!onSynchronize) return false;
		return onSynchronize();
	}
	QImage snapshotIcon(void) override { return icon; }
	bool allowsCapture(void) const override { return allowed; }
	bool hasImageContent(void) const override { return imageContent; }
	void notify(void) { emit clipboardChanged(); }
	void finish(quint64 request, const QImage &image) { emit iconReady(request, image); }
	int interval = 0;
	bool allowed = true;
	bool imageContent = true;
	QImage icon;
	quint64 lastRequest = 0;
	std::function<bool()> onSynchronize;
};

class ObservedMimeData final : public QMimeData
{
public:
	explicit ObservedMimeData(int &reads) : m_reads(reads) { setText("temporary content"); }
protected:
	QVariant retrieveData(const QString &type, QMetaType preferred) const override
	{
		++m_reads;
		return QMimeData::retrieveData(type, preferred);
	}
private:
	int &m_reads;
};

void capturePolicy(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	TestFeed feed;
	QClipboard &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);

	int reads = 0;
	feed.allowed = false;
	clipboard.setMimeData(new ObservedMimeData(reads)); feed.notify();
	controller.flushPending();
	require(history.entries().isEmpty(), "Excluded clipboard content entered history");
	require(repository.writes.isEmpty(), "Excluded content reached storage");
	require(reads == 0, "Excluded clipboard payload was read");
	require(feed.lastRequest == 0, "Excluded clipboard requested a source icon");

	/* An excluded write must cancel a previously scheduled normal copy. */
	feed.allowed = true;
	clipboard.setText("pending"); feed.notify();
	feed.allowed = false;
	clipboard.setMimeData(new ObservedMimeData(reads)); feed.notify();
	controller.flushPending();
	require(history.entries().isEmpty(), "Excluded replacement escaped pending cancellation");
	require(reads == 0, "Excluded replacement payload was read");

	/* Recheck the policy at capture time, even without a new notification. */
	feed.allowed = true; feed.notify();
	feed.allowed = false;
	controller.flushPending();
	require(history.entries().isEmpty(), "Late exclusion was not checked before capture");
	require(reads == 0, "Late exclusion allowed a payload read");

	/* A provider may add its marker while resolving a promised flavor. */
	int checks = 0;
	feed.allowed = true;
	feed.onSynchronize = [&] {
		if (++checks == 3) feed.allowed = false;
		return false;
	};
	clipboard.setText("marked while reading"); feed.notify();
	controller.flushPending();
	require(history.entries().isEmpty(), "Content marked during capture entered history");
	feed.onSynchronize = {};

	feed.allowed = true;
	clipboard.setText("ordinary copy"); feed.notify();
	controller.flushPending();
	require(history.entries().size() == 1, "Normal copy after exclusion was lost");
	require(history.entries().first()->mimeData->text() == "ordinary copy",
		"Excluded content replaced the subsequent normal copy");

	/* The location of an explicitly copied file is not an exclusion marker. */
	auto *files = new QMimeData;
	files->setUrls({QUrl::fromLocalFile("/tmp/Pastes test/cache/image.png")});
	clipboard.setMimeData(files); feed.notify(); controller.flushPending();
	require(history.entries().size() == 2, "A deliberate temporary file copy was lost");
}

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
	const auto previousCopy = history.entries().first()->events.copy;
	const auto originalSource = original->events.source;
	require(controller.copy(*original), "Internal copy failed");
	waitUntil([&] { return history.entries().first()->mimeData->text() == "first"; });
	require(history.entries().size() == 2, "Internal copy failed to deduplicate");
	const auto internalCopy = history.entries().first()->events.copy;
	require(
		internalCopy > previousCopy && history.entries().first()->events.source == originalSource,
		"Explicit internal copy reused a copy event or refreshed its inherited source");
	feed.notify();
	controller.flushPending();
	require(history.entries().first()->events.copy == internalCopy,
		"Own clipboard notification manufactured a second copy event");
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

void delayedCaptureRetainsObservationTime(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load();
	repository.finishLoad();
	TestFeed feed;
	feed.interval = 25;
	feed.icon = QImage(16, 16, QImage::Format_ARGB32);
	feed.icon.fill(Qt::green);
	QClipboard &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);
	clipboard.setText("copy delayed by platform settling");
	const auto before = QDateTime::currentDateTime();
	feed.notify();
	const auto after = QDateTime::currentDateTime();
	const auto request = feed.lastRequest;
	auto remote = textEntry("copy delayed by platform settling", after.addSecs(1));
	remote->events.copy = {after.addMSecs(100).toMSecsSinceEpoch(), 0, "remote"};
	remote->mimeData->setHtml("<b>newer remote copy</b>");
	history.mergeSynced(remote, {});
	waitUntil([&] { return !history.entries().first()->icon.isNull(); });
	const auto kept = history.entries().first();
	const auto observed = kept->events.source;
	require(kept == remote && kept->time == remote->time &&
			kept->events.copy == remote->events.copy &&
			kept->events.sourceCaptured >= before.toMSecsSinceEpoch() &&
			kept->events.sourceCaptured <= after.toMSecsSinceEpoch(),
		"Settling completion changed the copy winner or used completion time as observation time");
	QImage final(16, 16, QImage::Format_ARGB32);
	final.fill(Qt::red);
	feed.finish(request, final);
	waitUntil([&] { return kept->icon == final; });
	require(kept->events.source == observed && kept->events.sourceRevision == 1 &&
			kept->mimeData->html() == "<b>newer remote copy</b>",
		"Late source completion changed the original observation or rewound content");
}

void mixedImageSnapshot(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	TestFeed feed;
	QClipboard &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);
	const QList<QUrl> urls{QUrl::fromLocalFile("/tmp/nonexistent temporary image.png")};
	QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::cyan);
	auto *mime = new QMimeData;
	mime->setUrls(urls); mime->setImageData(image);
	clipboard.setMimeData(mime); feed.notify(); controller.flushPending();
	require(history.entries().size() == 1, "Mixed image copy was not captured");
	const auto entry = history.entries().first();
	require(entry->mimeData->urls() == urls && qvariant_cast<QImage>(entry->mimeData->imageData()) == image,
		"Capture discarded a mixed image format");
	require(controller.copy(*entry), "Mixed image copy could not be restored");
	const QMimeData *restored = clipboard.mimeData();
	require(restored->urls() == urls && qvariant_cast<QImage>(restored->imageData()) == image,
		"Restoring the image discarded its original formats");
	feed.notify(); controller.flushPending();
	require(history.entries().size() == 1, "Restoring a mixed image changed its persisted identity");
	auto *second = new QMimeData;
	const QList<QUrl> viewerUrls{QUrl::fromLocalFile("/tmp/viewer/another temporary image.png")};
	second->setUrls(viewerUrls); second->setImageData(image.convertToFormat(QImage::Format_RGB888));
	clipboard.setMimeData(second); feed.notify(); controller.flushPending();
	require(history.entries().size() == 1 && history.entries().first()->mimeData->urls() == viewerUrls,
		"A copy from another location retained a duplicate image or stale file format");
	feed.imageContent = false;
	auto *file = new QMimeData;
	file->setUrls({QUrl::fromLocalFile("/tmp/file-only.png")}); file->setImageData(image);
	clipboard.setMimeData(file); feed.notify(); controller.flushPending();
	require(history.entries().size() == 2 && !history.entries().first()->mimeData->hasImage(),
		"A synthesized file icon was retained as clipboard image content");
}

void clipboardOwnership(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	TestFeed feed;
	QClipboard &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, false);
	auto entry = textEntry("owned payload");
	entry->mimeData->setHtml("<b>owned payload</b>");
	entry->mimeData->setData("application/custom", QByteArray("a\0b", 3));
	QPointer<QMimeData> source = entry->mimeData.get();
	require(controller.copy(*entry), "Copying an owned history payload failed");
	QPointer<QMimeData> copied = const_cast<QMimeData *>(clipboard.mimeData());
	require(copied && copied != source && copied->html() == "<b>owned payload</b>" &&
		copied->data("application/custom") == QByteArray("a\0b", 3),
		"The clipboard did not receive an independent MIME snapshot");
	entry.clear();
	require(source.isNull() && copied && copied->text() == "owned payload",
		"Releasing history invalidated the system clipboard payload");
	clipboard.clear();
	require(copied.isNull(), "The system clipboard did not release its transferred payload");

	entry = textEntry("plain payload");
	entry->mimeData->setHtml("<b>plain payload</b>");
	require(controller.copy(*entry, true) && clipboard.text() == "plain payload" &&
		!clipboard.mimeData()->hasHtml() && entry->mimeData->hasHtml(),
		"Plain text copy changed the original payload or retained rich formats");

	/* Native clipboard notifications can synchronously replace a copied
	 * payload before copy() finishes updating the X11 selection. */
	QObject observer;
	bool replaced = false;
	QPointer<QMimeData> replacedPayload;
	QObject::connect(&clipboard, &QClipboard::dataChanged, &observer, [&] {
		if (std::exchange(replaced, true)) return;
		replacedPayload = const_cast<QMimeData *>(clipboard.mimeData());
		clipboard.setText("replacement during notification");
	});
	require(controller.copy(*entry) && replaced && replacedPayload.isNull() &&
		clipboard.text() == "replacement during notification" && entry->mimeData->hasHtml(),
		"Reentrant clipboard replacement invalidated the original history payload");
	if (clipboard.supportsSelection())
		require(clipboard.text(QClipboard::Selection) == "plain payload",
			"Reentrant replacement changed the prepared X11 selection snapshot");
	clipboard.clear();
	if (clipboard.supportsSelection()) clipboard.clear(QClipboard::Selection);
}

void sourceObservationClock(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load();
	repository.finishLoad();
	TestFeed feed;
	feed.icon = QImage(64, 64, QImage::Format_ARGB32);
	feed.icon.fill(Qt::blue);
	auto &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);
	clipboard.setText("observation clock");
	feed.notify();
	waitUntil([&] { return history.entries().size() == 1; });
	const auto original = history.entries().first();
	const qint64 observed = SourceIcon::captured(original->icon);
	require(observed == original->time.toMSecsSinceEpoch() && observed > 0 &&
			!SourceIcon::captured(feed.icon),
		"Native capture did not timestamp its own snapshot independently of the feed cache");
	waitUntil([&] { return QDateTime::currentMSecsSinceEpoch() > observed + 2; });
	require(controller.copy(*original), "Internal source copy failed");
	waitUntil([&] { return history.entries().first() != original; });
	require(SourceIcon::captured(history.entries().first()->icon) == observed,
		"Internal copying refreshed an inherited icon's observation time");
	feed.icon.fill(Qt::red);
	clipboard.setText("observation clock");
	feed.notify();
	waitUntil([&] { return SourceIcon::captured(history.entries().first()->icon) > observed; });
	require(history.entries().first()->icon == feed.icon,
		"A real native re-copy failed to replace the previous source icon");
}

void colorConvertedCapture(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	TestFeed feed;
	feed.icon = QImage(16, 16, QImage::Format_ARGB32); feed.icon.fill(Qt::blue);
	auto &clipboard = *QGuiApplication::clipboard();
	ClipboardController controller(history, feed, clipboard, true);
	clipboard.setImage(colorSpaceImage()); feed.notify(); controller.flushPending();
	require(history.entries().size() == 1, "First color image was not captured");
	const auto first = history.entries().first();
	const auto original = colorSpaceImage(true);
	auto *mime = new QMimeData;
	mime->setImageData(original);
	mime->setUrls({QUrl::fromLocalFile("/tmp/converted-copy.png")});
	feed.icon.fill(Qt::red);
	clipboard.setMimeData(mime); feed.notify(); controller.flushPending();
	require(history.entries().size() == 1 && history.entries().first()->events.copy > first->events.copy,
		"A cross-application color conversion created duplicate history");
	const auto latest = history.entries().first();
	require(latest->icon == feed.icon && latest->events.source == latest->events.copy &&
		qvariant_cast<QImage>(latest->mimeData->imageData()) == original &&
		latest->mimeData->urls().first().toLocalFile() == "/tmp/converted-copy.png",
		"Capture lost the latest source or changed its original image payload");
}

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	return runTest("settled captures retain event identity and original observation time",
			   delayedCaptureRetainsObservationTime) |
		runTest("clipboard pause, copy and native synchronization", clipboardLifecycle) |
		runTest("native captures timestamp source icons while internal copies retain provenance",
			sourceObservationClock) |
		runTest("color converted native copies merge without altering the latest image", colorConvertedCapture) |
		runTest("clipboard capture exclusion and recovery", capturePolicy) |
		runTest("mixed clipboard image snapshots preserve original formats", mixedImageSnapshot) |
		runTest("clipboard transfer, plain text and reentrant replacement", clipboardOwnership);
}
