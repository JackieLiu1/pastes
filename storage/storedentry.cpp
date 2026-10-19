#include "storage/storedentry.h"
#include "core/clipboarddata.h"

StoredEntry StoredEntry::snapshot(const ItemData &entry)
{
	StoredEntry result;
	result.events = entry.events;
	result.portable = SyncContent::snapshot(entry);
	result.md5 = entry.md5;
	result.contentKey = entry.contentKey;
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
	entry->events = events;
	entry->md5 = md5;
	entry->contentKey = contentKey;
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

StoredEntry StoredEntry::fromPortable(const SyncContent::Snapshot &value)
{
	StoredEntry result;
	result.md5 = value.md5;
	result.time = value.time;
	result.events = value.events;
	result.icon = value.icon;
	result.favorite = value.favorite;
	result.favoriteModified = value.favoriteModified;
	result.favoriteDetails = value.favoriteDetails;
	if (value.kind == "image") {
		result.hasImage = true;
		result.encodedImage = value.png;
		result.formats.append({QStringLiteral("application/x-qt-image"), QByteArray()});
	} else if (value.kind == "links") {
		QByteArray urls;
		for (const auto &url : value.urls)
			urls.append(url.toUtf8() + "\r\n");
		result.formats.append({QStringLiteral("text/uri-list"), urls});
	} else {
		result.formats.append({QStringLiteral("text/plain"), value.text.toUtf8()});
		if (!value.html.isEmpty())
			result.formats.append({QStringLiteral("text/html"), value.html.toUtf8()});
	}
	return result;
}
