#include "core/itemdata.h"
#include "core/clipboarddata.h"

HistoryEntry cloneEntry(const ItemData &source)
{
	auto entry = HistoryEntry::create();
	entry->mimeData = source.mimeData ? ClipboardData::duplicate(source.mimeData) : new QMimeData;
	entry->icon = source.icon;
	entry->md5 = source.md5;
	entry->time = source.time;
	return entry;
}
