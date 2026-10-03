#include "storage/storedentry.h"
#include "core/clipboarddata.h"

StoredEntry StoredEntry::snapshot(const ItemData &entry)
{
	StoredEntry result;
	result.md5 = entry.md5;
	result.time = entry.time;
	result.favorite = entry.favorite;
	result.favoriteModified = entry.favoriteModified;
	result.favoriteDetails = entry.favoriteDetails;
	result.icon = entry.icon;
	for (const QString &format : entry.mimeData->formats())
		result.formats.append({format, entry.mimeData->data(format)});
	result.hasImage = entry.mimeData->hasImage();
	if (result.hasImage) {
		result.encodedImage = ClipboardData::storedImage(entry.mimeData.get());
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
	entry->favorite = favorite;
	entry->favoriteModified = favoriteModified;
	entry->favoriteDetails = favoriteDetails;
	entry->icon = icon;
	entry->mimeData = std::make_unique<QMimeData>();
	for (const auto &format : formats)
		entry->mimeData->setData(format.first, format.second);
	if (entry->mimeData->hasImage() && !encodedImage.isEmpty()) {
		entry->mimeData = ClipboardData::withStoredImage(entry->mimeData.get(),
			encodedImage, QImage::Format_Invalid);
	}
	return entry;
}
