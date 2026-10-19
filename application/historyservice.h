#ifndef PASTES_HISTORYSERVICE_H
#define PASTES_HISTORYSERVICE_H

#include "application/historyrepository.h"
#include <QHash>
#include <QTimer>
#include <vector>

enum class HistoryChange { Loaded, Captured, Deleted, Replaced, Expired, Discarded, Restored, Synced };

class HistoryService final : public QObject
{
	Q_OBJECT
public:
	explicit HistoryService(HistoryRepository &repository, QObject *parent = nullptr);
	void load(void);
	void record(HistoryEntry entry, quint64 sourceRequest = 0);
	void setSourceIcon(quint64 request, const QImage &icon);
	bool remove(EntryId id);
	bool setFavorite(EntryId id, bool favorite);
	bool setFavoriteName(EntryId id, const QString &name);
	bool moveFavorite(EntryId id, EntryId neighbor);
	QList<HistoryEntry> favoriteEntries(void) const;
	void discard(EntryId id);
	void mergeSynced(HistoryEntry entry, const QList<QByteArray> &replaced);
	void applySynced(HistoryEntry entry, const QList<QByteArray> &replaced,
		const EventState &events, const QString &key, const QString &account, const QString &record,
		const SyncContent::Snapshot &metadata = {});
	EventVersion beginCopy(void) { return m_ready ? m_clock.tick() : EventVersion(); }
	void ensureDurable(const HistoryEntry &entry)
	{
		if (entry->contentKey.isEmpty()) persist(entry, {entry->md5});
	}
	void loadOutbox(const QString &account, quint64 token, const QSet<QString> &known = {})
	{
		m_repository.loadOutbox(account, token, known);
	}
	void markPublished(const QString &account, const QString &id)
	{
		m_repository.markPublished(account, id);
	}
	void retryPersistence(void);
	bool hasInFlightWrites(void) const
	{
		for (const auto &write : m_writes)
			if (!write.failed) return true;
		return false;
	}
	bool hasPersistenceFailures(void) const
	{
		for (const auto &write : m_writes)
			if (write.failed) return true;
		return false;
	}
	void clearUndo(void);
	bool canUndo(void) const { return !m_deleted.empty(); }
	EntryId nextUndoId(void) const;
	struct UndoResult {
		HistoryEntry entry;
		bool inserted = false;
	};
	UndoResult undo(void);
	const QList<HistoryEntry> &entries(void) const { return m_entries; }
	HistoryEntry find(EntryId id) const;

signals:
	void persistenceStateChanged(void);
	void durableChanged(QByteArray document);
	void syncApplied(QString account, QString record, bool success);
	void persistenceFailed(QString message);
	void appliedLoaded(QString account, quint64 token, QStringList ids);
	void outboxLoaded(QString account, quint64 token, QList<SyncContent::JournalRecord> documents);
	void published(QString account, QString id);
	void publishFailed(QString message);
	void syncWarning(QString message);
	void entryAdded(HistoryEntry entry, int row, HistoryChange change);
	void entryRemoved(EntryId id, HistoryChange change);
	void entryErasing(HistoryEntry entry, HistoryChange change);
	/* A synced duplicate or normalized key is replaced by entryAdded. */
	void entryReplacing(EntryId previous, EntryId replacement);
	void entryChanged(EntryId id);
	void favoriteChanged(HistoryEntry entry);
	void loaded(void);
	void undoChanged(bool available);

private:
	struct Neighbor { QByteArray md5; QDateTime time; };
	struct DeletedEntry {
		HistoryEntry entry;
		EntryId originalId;
		std::vector<Neighbor> neighbors;
		int row;
	};
	struct PendingCapture { HistoryEntry entry; quint64 sourceRequest; };
	struct SourceRequest {
		EntryId entry = 0;
		qint64 captured = 0;
		EventVersion version;
	};
	void acceptLoaded(const QList<HistoryEntry> &entries);
	void erase(int row, HistoryChange change, bool write = true);
	struct PendingWrite {
		HistoryEntry entry;
		QList<QByteArray> removed;
		bool deleted = false;
		QString key, account, record;
		bool failed = false;
		bool publish = true;
	};
	void persist(const HistoryEntry &entry, const QList<QByteArray> &removed = {},
		bool deleted = false, const QString &key = {}, const QString &account = {},
		const QString &record = {}, bool publish = true);
	void persistHousekeeping(const HistoryEntry &entry, const QList<QByteArray> &removed)
	{
		persist(entry, removed, false, {}, {}, {}, false);
	}
	void mergeIncoming(HistoryEntry entry, const QList<QByteArray> &replaced,
		const EventState &events, const QString &key, const QString &account, const QString &record,
		const SyncContent::Snapshot &metadata = {}, bool persistResult = true);
	struct Incoming {
		HistoryEntry entry;
		QList<QByteArray> replaced;
		EventState events;
		QString key, account, record;
		SyncContent::Snapshot metadata;
		QSet<quint64> barrier;
	};
	void drainIncoming(void);
	QList<Incoming> m_incoming;
	QHash<QByteArray, QString> m_contentKeys;
	EventClock m_clock;
	QHash<quint64, PendingWrite> m_writes;
	void persistFavorite(const HistoryEntry &entry);
	void initializeFavoritePosition(const HistoryEntry &entry);
	int indexOf(EntryId id) const;
	int restorationRow(const DeletedEntry &removed) const;
	static bool sameContent(const HistoryEntry &left, const HistoryEntry &right);

	HistoryRepository &m_repository;
	QList<HistoryEntry> m_entries;
	QHash<quint64, SourceRequest> m_sourceRequests;
	QHash<quint64, EntryId> m_imageRequests;
	std::vector<DeletedEntry> m_deleted;
	QList<PendingCapture> m_pendingCaptures;
	QTimer m_undoTimer;
	EntryId m_nextId = 0;
	bool m_loadRequested = false;
	bool m_ready = false;
};

#endif
