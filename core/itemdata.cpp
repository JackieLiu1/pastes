#include "core/itemdata.h"
#include "core/clipboarddata.h"

bool FavoriteDetails::operator==(const FavoriteDetails &other) const
{
	return name == other.name && nameModified == other.nameModified &&
		position == other.position && positionModified == other.positionModified;
}

bool mergeFavoriteDetails(FavoriteDetails &target, const FavoriteDetails &source)
{
	const auto before = target;
	/* Name and position are independent edits. Resolve equal clocks the same
	 * way on every device, without manufacturing another edit timestamp. */
	if (source.nameModified > target.nameModified || (source.nameModified > 0 &&
		source.nameModified == target.nameModified && source.name > target.name)) {
		target.name = source.name;
		target.nameModified = source.nameModified;
	}
	if (source.positionModified > target.positionModified || (source.positionModified > 0 &&
		source.positionModified == target.positionModified && source.position < target.position)) {
		target.position = source.position;
		target.positionModified = source.positionModified;
	}
	return target != before;
}

HistoryEntry cloneEntry(const ItemData &source)
{
	auto entry = HistoryEntry::create();
	entry->mimeData = source.mimeData ? ClipboardData::duplicate(source.mimeData) : new QMimeData;
	entry->icon = source.icon;
	entry->md5 = source.md5;
	entry->imageContentKey = source.imageContentKey;
	entry->time = source.time;
	entry->favorite = source.favorite;
	entry->favoriteModified = source.favoriteModified;
	entry->favoriteDetails = source.favoriteDetails;
	return entry;
}
