#include "tests/testsupport.h"
#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "core/historypolicy.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QUrl>

namespace {

void fingerprintContract(void)
{
	QMimeData plain;
	plain.setText("  same text  ");
	const QByteArray expected = QCryptographicHash::hash(QByteArray("same text"), QCryptographicHash::Md5);
	require(ClipboardContent::fingerprint(plain) == expected, "Text identity changed");
	plain.setHtml("<b>different markup</b>");
	require(ClipboardContent::fingerprint(plain) == expected, "HTML must use plain text identity");
	const QList<QUrl> urls{QUrl::fromLocalFile("/one"), QUrl::fromLocalFile("/two")};
	plain.setUrls(urls);
	const QByteArray urlBytes = urls[0].toEncoded()+urls[1].toEncoded();
	require(ClipboardContent::fingerprint(plain) == QCryptographicHash::hash(urlBytes, QCryptographicHash::Md5), "URL precedence changed");
	QImage image(17, 9, QImage::Format_ARGB32);
	image.fill(QColor(10, 20, 30, 255));
	QMimeData bitmap;
	bitmap.setImageData(image);
	const QByteArray pixels(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes());
	require(ClipboardContent::fingerprint(bitmap) == QCryptographicHash::hash(pixels, QCryptographicHash::Md5), "Image row bytes changed");
	QMimeData empty;
	empty.setText(" \n ");
	require(ClipboardContent::fingerprint(empty).isEmpty(), "Empty clipboard must be ignored");
}

void retentionAndStartup(void)
{
	const QDateTime now = QDateTime::currentDateTime();
	require(!HistoryPolicy::expired(now.addDays(-30).addSecs(1), now), "Retention expired early");
	require(HistoryPolicy::expired(now.addDays(-30), now), "Thirty-day boundary changed");
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); history.load();
	require(repository.loads == 1, "Repeated load must not duplicate history");
	history.record(textEntry("pending"), 7);
	QImage icon(4, 4, QImage::Format_ARGB32); icon.fill(Qt::green);
	history.setSourceIcon(7, icon);
	repository.finishLoad({textEntry("expired", now.addDays(-31)), textEntry("pending", now.addDays(-1)), textEntry("kept")});
	require(history.entries().size() == 2, "Startup capture must deduplicate loaded history");
	require(history.entries().first()->mimeData->text() == "pending", "Startup capture must stay newest");
	require(history.entries().first()->icon == icon, "Pending source icon lost during load");
	require(repository.removals.size() == 2, "Expired and replaced history must be removed");
}

void duplicateAndSourceIdentity(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	QImage known(3, 3, QImage::Format_ARGB32); known.fill(Qt::green);
	auto old = textEntry("duplicate"); old->icon = known;
	history.record(old, 1);
	history.record(textEntry("duplicate"), 2);
	require(history.entries().size() == 1, "Duplicate history retained");
	require(history.entries().first()->icon == known, "Empty lookup erased source icon");
	QImage late(3, 3, QImage::Format_ARGB32); late.fill(Qt::red);
	history.setSourceIcon(1, late);
	require(history.entries().first()->icon == known, "Stale source request changed replacement");
	history.setSourceIcon(2, late);
	require(history.entries().first()->icon == late, "Live source request was ignored");
	history.record(textEntry("duplicate"));
	history.setSourceIcon(2, known);
	require(history.entries().first()->icon == late, "Internal copy accepted an old native source");
}

QList<HistoryEntry> neighbors(void)
{
	QList<HistoryEntry> result;
	const QDateTime time = QDateTime::currentDateTime().addSecs(-10);
	for (const char *text : {"a", "b", "c", "d", "e"}) result.append(textEntry(text, time));
	return result;
}

QString order(const HistoryService &history)
{
	QStringList texts;
	for (const auto &entry : history.entries()) texts.append(entry->mimeData->text());
	return texts.join(',');
}

void undoPositions(void)
{
	for (int scenario = 0; scenario < 4; ++scenario) {
		MemoryRepository repository;
		HistoryService history(repository);
		history.load(); repository.finishLoad(neighbors());
		history.remove(history.entries().first()->id);
		if (scenario == 1) history.record(textEntry("new"));
		if (scenario == 2) history.record(textEntry("c"));
		if (scenario == 3) history.remove(history.entries().first()->id);
		const auto result = history.undo();
		require(result.inserted, "Undo must insert missing content");
		if (scenario == 0) require(order(history) == "a,b,c,d,e", "First-row undo moved the card");
		if (scenario == 1) require(order(history) == "new,a,b,c,d,e", "New copies must stay before restored history");
		if (scenario == 2) require(order(history) == "c,a,b,d,e", "Promoted neighbors must not move restored history");
		if (scenario == 3) {
			history.undo();
			require(order(history) == "a,b,c,d,e", "Adjacent undo lost original positions");
		}
	}
}

void undoRecopyAndLimit(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad(neighbors());
	history.remove(history.entries().first()->id);
	history.record(textEntry("a"));
	const EntryId fresh = history.entries().first()->id;
	const auto result = history.undo();
	require(!result.inserted && result.entry->id == fresh, "Undo duplicated a fresh copy");
	require(history.entries().size() == 5, "Undo re-copy changed history count");
	for (int i = 0; i < 21; ++i) {
		history.record(textEntry(QString::number(i)));
		history.remove(history.entries().first()->id);
	}
	int count = 0;
	while (history.canUndo()) { history.undo(); ++count; }
	require(count == 20, "Undo retention limit changed");
	history.remove(history.entries().first()->id);
	history.clearUndo();
	require(!history.canUndo(), "Expired undo remained available");
}

void imageRequestsAndSnapshots(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	history.load(); repository.finishLoad();
	auto imageEntry = HistoryEntry::create();
	imageEntry->mimeData = new QMimeData;
	QImage image(48, 24, QImage::Format_ARGB32); image.fill(Qt::blue);
	image.setDevicePixelRatio(2);
	imageEntry->mimeData->setImageData(image);
	imageEntry->md5 = ClipboardContent::fingerprint(*imageEntry->mimeData);
	history.record(imageEntry);
	const quint64 stale = repository.request;
	history.remove(imageEntry->id);
	const auto restored = history.undo().entry;
	const quint64 live = repository.request;
	const QByteArray bytes = png(image);
	repository.finishImage(stale, bytes, image.format(), 2);
	require(ClipboardData::storedImage(restored->mimeData).isEmpty(), "Stale encoding changed restored content");
	repository.finishImage(live, bytes, image.format(), 2);
	require(ClipboardData::storedImage(restored->mimeData) == bytes, "Live encoding was not retained");
	const auto snapshot = cloneEntry(*restored);
	history.remove(restored->id);
	const QImage decoded = qvariant_cast<QImage>(snapshot->mimeData->imageData());
	require(decoded.size() == image.size() && decoded.devicePixelRatio() == 2, "Snapshot lost original image properties");
	require(decoded.pixelColor(0, 0) == image.pixelColor(0, 0), "Snapshot lost original pixels");
	QImage hdr(2, 2, QImage::Format_RGBA64);
	require(!ClipboardData::canCompressImage(hdr), "High precision image must remain uncompressed");
}

}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	failures += runTest("content identity", fingerprintContract);
	failures += runTest("retention and startup", retentionAndStartup);
	failures += runTest("deduplication and source identity", duplicateAndSourceIdentity);
	failures += runTest("undo original positions", undoPositions);
	failures += runTest("undo re-copy and retention", undoRecopyAndLimit);
	failures += runTest("image requests and independent snapshots", imageRequestsAndSnapshots);
	return failures ? 1 : 0;
}
