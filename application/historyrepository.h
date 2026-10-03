#ifndef PASTES_HISTORYREPOSITORY_H
#define PASTES_HISTORYREPOSITORY_H

#include "core/itemdata.h"
#include <QObject>

/* GUI-thread port. Implementations snapshot requests before returning and
 * deliver insert completions asynchronously on this object's thread.
 * Background work never borrows caller-owned pointers.
 * The repository outlives HistoryService. */
class HistoryRepository : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	virtual void load(void) = 0;
	virtual quint64 insert(const HistoryEntry &entry) = 0;
	virtual void remove(const QByteArray &md5) = 0;
	virtual void updateIcon(const QByteArray &md5, const QImage &icon) = 0;
	virtual void updateFavorite(const QByteArray &md5, bool favorite, qint64 modified, const FavoriteDetails &details) = 0;

signals:
	void loaded(QList<HistoryEntry> entries);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);
	void failed(QString message);
};

#endif
