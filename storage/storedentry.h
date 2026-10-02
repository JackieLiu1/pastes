#ifndef PASTES_STOREDENTRY_H
#define PASTES_STOREDENTRY_H

#include "core/itemdata.h"

/* Only value types cross the SQL worker boundary. Formats retain their
 * original order; image encoding stays on the database thread. */
struct StoredEntry
{
	QByteArray md5;
	QDateTime time;
	bool favorite = false;
	qint64 favoriteModified = 0;
	QImage icon;
	QList<QPair<QString, QByteArray>> formats;
	QByteArray encodedImage;
	QImage image;
	bool hasImage = false;

	static StoredEntry snapshot(const ItemData &entry);
	HistoryEntry materialize(void) const;
};
Q_DECLARE_METATYPE(StoredEntry)
Q_DECLARE_METATYPE(QList<StoredEntry>)

#endif
