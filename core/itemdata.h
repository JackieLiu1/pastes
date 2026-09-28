#ifndef PASTES_ITEMDATA_H
#define PASTES_ITEMDATA_H

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QMimeData>
#include <QSharedPointer>

using EntryId = quint64;

/* GUI-thread payload. Views and previews may retain an entry after removal.
 * Persistence receives value snapshots, never this object or its MIME data. */
struct ItemData final
{
	ItemData(void) = default;
	~ItemData(void) { delete mimeData; }
	ItemData(const ItemData &) = delete;
	ItemData &operator=(const ItemData &) = delete;

	EntryId id = 0;
	QMimeData *mimeData = nullptr;
	QImage icon;
	QByteArray md5;
	QDateTime time;
};

using HistoryEntry = QSharedPointer<ItemData>;
Q_DECLARE_METATYPE(HistoryEntry)
Q_DECLARE_METATYPE(QList<HistoryEntry>)

HistoryEntry cloneEntry(const ItemData &source);

#endif
