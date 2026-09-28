#include "storage/storedentry.h"
#include "core/clipboarddata.h"

StoredEntry StoredEntry::snapshot(const ItemData &entry)
{
	StoredEntry result;
	result.md5 = entry.md5;
	result.time = entry.time;
	result.icon = entry.icon;
	for (const QString &format : entry.mimeData->formats())
		result.formats.append({format, entry.mimeData->data(format)});
	result.hasImage = entry.mimeData->hasImage();
	if (result.hasImage) {
		result.encodedImage = ClipboardData::storedImage(entry.mimeData);
		if (result.encodedImage.isEmpty())
			result.image = qvariant_cast<QImage>(entry.mimeData->imageData());
	}
	return result;
}

HistoryEntry StoredEntry::materialize(void) const
{
	auto entry = HistoryEntry::create();
	entry->md5 = md5;
	entry->time = time;
	entry->icon = icon;
	entry->mimeData = new QMimeData;
	for (const auto &format : formats)
		entry->mimeData->setData(format.first, format.second);
	if (entry->mimeData->hasImage() && !encodedImage.isEmpty()) {
		QMimeData *mime = ClipboardData::withStoredImage(entry->mimeData, encodedImage, QImage::Format_Invalid);
		delete entry->mimeData;
		entry->mimeData = mime;
	}
	return entry;
}
