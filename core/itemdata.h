#ifndef PASTES_ITEMDATA_H
#define PASTES_ITEMDATA_H

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QMimeData>
#include <QSharedPointer>
#include <memory>

using EntryId = quint64;

struct FavoriteDetails {
	static constexpr int maxNameLength = 80;
	static constexpr qint64 maxPosition = qint64(1) << 50;
	QString name;
	qint64 nameModified = 0;
	qint64 position = 0;
	qint64 positionModified = 0;
	bool operator==(const FavoriteDetails &other) const;
	bool operator!=(const FavoriteDetails &other) const { return !(*this == other); }
};
Q_DECLARE_METATYPE(FavoriteDetails)
bool mergeFavoriteDetails(FavoriteDetails &target, const FavoriteDetails &source);

/* GUI-thread payload. Views and previews may retain an entry after removal.
 * Persistence receives value snapshots, never this object or its MIME data. */
struct ItemData final
{
	ItemData(void) = default;
	~ItemData(void) = default;
	ItemData(const ItemData &) = delete;
	ItemData &operator=(const ItemData &) = delete;

	EntryId id = 0;
	std::unique_ptr<QMimeData> mimeData;
	QImage icon;
	QByteArray md5;
	/* Cached pixel comparison; the persisted MD5 and original MIME stay intact. */
	QByteArray imageContentKey;
	QDateTime time;
	bool favorite = false;
	qint64 favoriteModified = 0;
	FavoriteDetails favoriteDetails;
};

using HistoryEntry = QSharedPointer<ItemData>;
Q_DECLARE_METATYPE(HistoryEntry)
Q_DECLARE_METATYPE(QList<HistoryEntry>)

HistoryEntry cloneEntry(const ItemData &source);

#endif
