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
	void entryAdded(HistoryEntry entry, int row, HistoryChange change);
	void entryRemoved(EntryId id, HistoryChange change);
	void entryErasing(HistoryEntry entry, HistoryChange change);
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
	void acceptLoaded(const QList<HistoryEntry> &entries);
	void erase(int row, HistoryChange change);
	void persist(const HistoryEntry &entry);
	void persistFavorite(const HistoryEntry &entry);
	void initializeFavoritePosition(const HistoryEntry &entry);
	int indexOf(EntryId id) const;
	int restorationRow(const DeletedEntry &removed) const;
	static bool sameContent(const HistoryEntry &left, const HistoryEntry &right);

	HistoryRepository &m_repository;
	QList<HistoryEntry> m_entries;
	QHash<quint64, EntryId> m_sourceRequests;
	QHash<quint64, EntryId> m_imageRequests;
	std::vector<DeletedEntry> m_deleted;
	QList<PendingCapture> m_pendingCaptures;
	QTimer m_undoTimer;
	EntryId m_nextId = 0;
	bool m_loadRequested = false;
	bool m_ready = false;
};

#endif
