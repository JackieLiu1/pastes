#ifndef PASTES_HISTORYREPOSITORY_H
#define PASTES_HISTORYREPOSITORY_H

#include "core/itemdata.h"
#include "core/synccontent.h"
#include <QSet>
#include <QObject>

struct HistoryWrite {
	QList<QByteArray> removed;
	bool deleted = false;
	bool publish = true;
	QString key, account, record;
	/* Retire older portable image identities in the replacement transaction. */
	QSet<QString> retiredImageKeys = {};
};
Q_DECLARE_METATYPE(HistoryWrite)

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

	virtual quint64 save(const HistoryEntry &entry, const HistoryWrite &write = {}) = 0;
	virtual void loadOutbox(
		const QString &account, quint64 token, const QSet<QString> &known = {}) = 0;
	virtual void markPublished(const QString &account, const QString &id) = 0;

signals:
	void clockLoaded(QString device, EventVersion version);
	void projected(quint64 request, HistoryEntry entry, bool deleted);
	void committed(quint64 request, QByteArray document);
	void commitFailed(quint64 request, QString message);
	void appliedLoaded(QString account, quint64 token, QStringList ids);
	void outboxLoaded(QString account, quint64 token, QList<SyncContent::JournalRecord> documents);
	void published(QString account, QString id);
	void publishFailed(QString message);
	void syncWarning(QString message);
	void loaded(QList<HistoryEntry> entries);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);
	void failed(QString message);
};

#endif
