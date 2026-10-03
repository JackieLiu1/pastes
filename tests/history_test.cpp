#include "tests/testsupport.h"
#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "core/historypolicy.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QImageReader>
#include <QPointer>
#include <QUrl>

namespace {

void payloadLifetime(void)
{
	auto entry = textEntry("retained payload");
	auto retained = entry;
	auto snapshot = cloneEntry(*entry);
	QPointer<QMimeData> original = entry->mimeData.get();
	entry.clear();
	require(original && retained->mimeData->text() == "retained payload",
		"Releasing one consumer destroyed a shared history payload");
	retained->mimeData = ClipboardData::duplicate(retained->mimeData.get());
	require(original.isNull(), "Replacing a history payload leaked its previous MIME data");
	retained->mimeData->setText("replacement");
	require(snapshot->mimeData->text() == "retained payload",
		"Replacing a live entry modified an independent snapshot");
	QPointer<QMimeData> replacement = retained->mimeData.get();
	retained.clear();
	require(replacement.isNull(), "The last history consumer did not release its payload");
	QPointer<QMimeData> snapshotPayload = snapshot->mimeData.get();
	snapshot.clear();
	require(snapshotPayload.isNull(), "The last snapshot consumer did not release its payload");
}

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
	const QUrl picture = QUrl::fromLocalFile("/one.png");
	bitmap.setUrls({picture});
	require(ClipboardContent::prefersImage(bitmap), "An accompanying local path concealed supplied pixels");
	require(ClipboardContent::fingerprint(bitmap) == QCryptographicHash::hash(picture.toEncoded(), QCryptographicHash::Md5),
		"Presenting mixed content as an image changed persisted identity");
	require(!ClipboardContent::prefersImage(plain), "File-only content was classified as an image");
	for (const char *path : {"/document.docx", "/vector.svg", "/vector.svgz"}) {
		bitmap.setUrls({QUrl::fromLocalFile(path)});
		require(!ClipboardContent::prefersImage(bitmap), "A file icon was treated as copied image content");
	}
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

HistoryEntry imageEntry(const QImage &image, const QString &path, QDateTime time)
{
	auto entry = HistoryEntry::create();
	entry->mimeData = std::make_unique<QMimeData>();
	entry->mimeData->setImageData(image);
	if (!path.isEmpty()) entry->mimeData->setUrls({QUrl::fromLocalFile(path)});
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	entry->time = time;
	return entry;
}

void imageContentIdentity(void)
{
	QImage image(5, 7, QImage::Format_RGB32); image.fill(Qt::green);
	QImage padded = image.convertToFormat(QImage::Format_RGB888);
	for (int row = 0; row < padded.height(); ++row) padded.scanLine(row)[15] = 42;
	padded.setDevicePixelRatio(2);
	require(ClipboardContent::imageContentKey(image) == ClipboardContent::imageContentKey(padded),
		"Pixel identity depended on packing, row padding or display scale");
	QImage reshaped(7, 5, QImage::Format_RGB32); reshaped.fill(Qt::green);
	require(ClipboardContent::imageContentKey(image) != ClipboardContent::imageContentKey(reshaped),
		"Images with different dimensions shared an identity");
	QImage precise(1, 1, QImage::Format_RGBA64);
	precise.setPixelColor(0, 0, QColor::fromRgba64(65534, 0, 0, 65535));
	QImage different = precise;
	different.setPixelColor(0, 0, QColor::fromRgba64(65535, 0, 0, 65535));
	require(ClipboardContent::imageContentKey(precise) != ClipboardContent::imageContentKey(different),
		"Pixel comparison discarded high precision samples");
}

void imageCopiesAndUndo(void)
{
	MemoryRepository repository;
	HistoryService history(repository); history.load(); repository.finishLoad();
	const QDateTime now = QDateTime::currentDateTime();
	QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::cyan);
	const auto first = imageEntry(image, "/tmp/chat/first.png", now.addSecs(-10));
	first->icon = image;
	history.record(first);
	const auto second = imageEntry(image.convertToFormat(QImage::Format_RGB888), "/tmp/viewer/second.png", now);
	require(first->md5 != second->md5, "Fixture did not retain distinct persisted URL identities");
	history.record(second);
	require(history.entries().size() == 1 && history.entries().first() == second,
		"The same pixels with different temporary URLs created two items");
	require(second->time == now && second->icon == image && second->mimeData->urls().first().toLocalFile() == "/tmp/viewer/second.png",
		"Image replacement lost the latest time, original MIME or known icon");
	history.remove(second->id);
	const auto fresh = imageEntry(image, "", now.addSecs(1)); history.record(fresh);
	const auto undone = history.undo();
	require(!undone.inserted && undone.entry == fresh && history.entries().size() == 1,
		"Undo duplicated an image re-copied without its file URL");
	QImage other = image; other.fill(Qt::red);
	history.record(imageEntry(other, "/tmp/other.png", now));
	require(history.entries().size() == 2, "Different pixels were merged");
	for (const char *path : {"/tmp/first.png", "/tmp/second.png"}) {
		auto file = textEntry("file reference");
		file->mimeData->setUrls({QUrl::fromLocalFile(path)});
		file->md5 = ClipboardContent::fingerprint(*file->mimeData);
		history.record(file);
	}
	require(history.entries().size() == 4, "File-only references were merged with images or each other");
	history.record(imageEntry(image, "/tmp/one.svg", now));
	history.record(imageEntry(image, "/tmp/two.svg", now));
	require(history.entries().size() == 6, "Different files were merged through their identical icon pixels");
}

void storedImageDuplicates(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	const QDateTime now = QDateTime::currentDateTime();
	QImage image(1024, 1024, QImage::Format_RGB32); image.fill(Qt::blue);
	const QByteArray bytes = png(image);
	const auto older = imageEntry(image, "/tmp/chat/one.png", now.addSecs(-10));
	const auto newer = imageEntry(image, "/tmp/viewer/two.png", now);
	for (const auto &entry : {newer, older}) {
		entry->mimeData = ClipboardData::withStoredImage(entry->mimeData.get(), bytes, QImage::Format_Invalid);
	}
	/* Startup must not expand full images just to collapse identical originals. */
	struct DecodeLimit {
		int previous = QImageReader::allocationLimit();
		DecodeLimit(void) { QImageReader::setAllocationLimit(1); }
		~DecodeLimit(void) { QImageReader::setAllocationLimit(previous); }
	} limit;
	history.load(); repository.finishLoad({newer, older});
	require(history.entries().size() == 1 && history.entries().first() == newer,
		"Restart retained duplicate stored images or decoded their full pixels");
	require(repository.removals == QList<QByteArray>{older->md5}, "Startup removed the wrong stored image");
}

void syncedImageDuplicates(void)
{
	MemoryRepository repository;
	HistoryService history(repository); history.load(); repository.finishLoad();
	QImage image(32, 24, QImage::Format_RGB32); image.fill(Qt::cyan);
	const QDateTime now = QDateTime::currentDateTime();
	const auto local = imageEntry(image, "/tmp/local.png", now);
	history.record(local);
	const auto stale = imageEntry(image.convertToFormat(QImage::Format_RGBA8888), "", now.addSecs(-10));
	history.mergeSynced(stale, {});
	require(history.entries().size() == 1 && history.entries().first() == local,
		"Older remote pixels duplicated or replaced a newer local image");
	const auto recent = imageEntry(image.convertToFormat(QImage::Format_RGBA8888), "", now.addSecs(1));
	history.mergeSynced(recent, {});
	require(history.entries().size() == 1 && history.entries().first() == recent,
		"A newer remote image did not replace its local duplicate");
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

void favoritesLifetime(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	const auto oldTime = QDateTime::currentDateTime().addDays(-45);
	auto saved = textEntry("saved", oldTime); saved->favorite = true; saved->favoriteModified = oldTime.toMSecsSinceEpoch();
	history.load(); repository.finishLoad({saved, textEntry("expired ordinary", oldTime)});
	require(history.entries().size() == 1 && history.entries().first() == saved,
		"Favorite expired with ordinary history at startup");
	history.record(textEntry("unrelated"));
	require(!history.find(saved->id).isNull(), "A new capture expired a favorite");
	const auto recopy = textEntry("saved"); history.record(recopy);
	require(recopy->favorite && recopy->favoriteModified == saved->favoriteModified,
		"Re-copy lost the favorite metadata");
	history.remove(recopy->id);
	const auto restored = history.undo();
	require(restored.inserted && restored.entry->favorite, "Delete/undo lost a favorite");
	require(history.setFavorite(restored.entry->id, false) && history.find(restored.entry->id),
		"Unstarring recent content deleted it");
	auto old = textEntry("old favorite", oldTime); old->favorite = true;
	history.mergeSynced(old, {});
	require(!history.find(old->id).isNull(), "Incoming old favorite was rejected");
	bool removalPublished = false;
	QObject::connect(&history, &HistoryService::favoriteChanged, &history, [&](HistoryEntry entry) {
		if (entry == old) removalPublished = !entry->favorite && entry->time == oldTime && history.find(entry->id);
	});
	require(history.setFavorite(old->id, false) && !history.find(old->id) && removalPublished,
		"Expired unfavorite did not publish its state before removal");
	const int updates = repository.favoriteUpdates.size();
	require(!history.setFavorite(old->id, true) && repository.favoriteUpdates.size() == updates,
		"A stale card identity changed favorite metadata");

	auto remote = cloneEntry(*restored.entry); remote->favorite = true;
	remote->favoriteModified = restored.entry->favoriteModified+1;
	history.mergeSynced(remote, {});
	require(restored.entry->favorite && history.find(restored.entry->id) == restored.entry,
		"A metadata-only sync replaced or ignored the existing entry");
	auto stale = cloneEntry(*remote); stale->favorite = false; stale->favoriteModified = 0;
	history.mergeSynced(stale, {});
	require(restored.entry->favorite, "Legacy sync cleared a newer favorite");
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
	imageEntry->mimeData = std::make_unique<QMimeData>();
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
	require(ClipboardData::storedImage(restored->mimeData.get()).isEmpty(), "Stale encoding changed restored content");
	repository.finishImage(live, bytes, image.format(), 2);
	require(ClipboardData::storedImage(restored->mimeData.get()) == bytes, "Live encoding was not retained");
	const auto snapshot = cloneEntry(*restored);
	history.remove(restored->id);
	const QImage decoded = qvariant_cast<QImage>(snapshot->mimeData->imageData());
	require(decoded.size() == image.size() && decoded.devicePixelRatio() == 2, "Snapshot lost original image properties");
	require(decoded.pixelColor(0, 0) == image.pixelColor(0, 0), "Snapshot lost original pixels");
	QImage hdr(2, 2, QImage::Format_RGBA64);
	require(!ClipboardData::canCompressImage(hdr), "High precision image must remain uncompressed");
}

void favoriteNamesAndOrder(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	auto first = textEntry("first payload"), second = textEntry("second payload"), third = textEntry("third payload");
	history.load(); repository.finishLoad({first, second, third});
	for (const auto &entry : {first, second, third}) history.setFavorite(entry->id, true);
	const auto copied = first->time;
	const auto hash = first->md5;
	require(history.setFavoriteName(first->id, "  常用 命令  "), "Favorite could not be named");
	require(first->favoriteDetails.name == "常用 命令" && first->time == copied && first->md5 == hash &&
		first->mimeData->text() == "first payload", "Naming modified clipboard content or copy identity");
	const auto named = first->favoriteDetails;
	require(history.moveFavorite(first->id, second->id), "Favorite could not be reordered");
	require(history.favoriteEntries() == QList<HistoryEntry>({second, first, third}), "Favorite ordering did not swap positions");
	require(first->favoriteDetails.nameModified == named.nameModified, "Reordering changed the name clock");
	auto recopy = textEntry("first payload"); history.record(recopy);
	require(recopy->favoriteDetails == first->favoriteDetails && history.favoriteEntries()[1] == recopy,
		"Re-copy reset the name or fixed position");
	const auto details = recopy->favoriteDetails;
	history.remove(recopy->id);
	const auto restored = history.undo().entry;
	require(restored->favoriteDetails == details && history.favoriteEntries()[1] == restored,
		"Undo reset favorite details");
	auto remote = cloneEntry(*restored);
	remote->time = copied.addSecs(-10);
	remote->favoriteDetails.name = "remote name";
	remote->favoriteDetails.nameModified += 100;
	remote->favoriteDetails.position = 999;
	remote->favoriteDetails.positionModified = 1;
	history.mergeSynced(remote, {});
	require(restored->favoriteDetails.name == "remote name" && restored->favoriteDetails.position == details.position &&
		restored->time == recopy->time, "Independent remote name edit overwrote order or copy time");
	require(history.setFavoriteName(restored->id, ""), "Clearing a favorite name failed");
	history.setFavoriteName(restored->id, QString(200, 'x'));
	require(restored->favoriteDetails.name.size() == FavoriteDetails::maxNameLength, "Favorite name length was unbounded");
	history.setFavoriteName(restored->id, QString(79, 'x')+QString::fromUtf8("😀tail"));
	require(restored->favoriteDetails.name.size() == 79 &&
		QString::fromUtf8(restored->favoriteDetails.name.toUtf8()) == restored->favoriteDetails.name,
		"Favorite name truncation split a Unicode character");
	history.setFavorite(restored->id, false);
	require(!history.setFavoriteName(restored->id, "ordinary") && !history.moveFavorite(restored->id, second->id),
		"Favorite commands modified ordinary history");

	MemoryRepository legacyRepository; HistoryService legacy(legacyRepository);
	auto old = textEntry("legacy"), other = textEntry("legacy other");
	old->favorite = other->favorite = true;
	old->favoriteModified = 100; other->favoriteModified = 200;
	legacy.load(); legacyRepository.finishLoad({other, old});
	require(legacy.favoriteEntries().first() == old && old->favoriteDetails.positionModified == 100,
		"Legacy favorite order was not migrated deterministically");
	other->favoriteDetails.position = old->favoriteDetails.position;
	const auto tied = legacy.favoriteEntries();
	legacy.moveFavorite(tied.last()->id, tied.first()->id);
	require(legacy.favoriteEntries().first() == tied.last(), "Equal device ranks prevented moving favorites");
}

}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	failures += runTest("shared payload replacement and snapshot lifetime", payloadLifetime);
	failures += runTest("content identity", fingerprintContract);
	failures += runTest("retention and startup", retentionAndStartup);
	failures += runTest("deduplication and source identity", duplicateAndSourceIdentity);
	failures += runTest("image content ignores packing and retains precision", imageContentIdentity);
	failures += runTest("image copies, file references and undo", imageCopiesAndUndo);
	failures += runTest("stored image duplicates without startup decoding", storedImageDuplicates);
	failures += runTest("synced image duplicates preserve newest copy time", syncedImageDuplicates);
	failures += runTest("favorite retention, recopy, undo and remote metadata", favoritesLifetime);
	failures += runTest("favorite names, fixed order and independent metadata", favoriteNamesAndOrder);
	failures += runTest("undo original positions", undoPositions);
	failures += runTest("undo re-copy and retention", undoRecopyAndLimit);
	failures += runTest("image requests and independent snapshots", imageRequestsAndSnapshots);
	return failures ? 1 : 0;
}
