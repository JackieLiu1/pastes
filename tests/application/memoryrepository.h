#ifndef PASTES_MEMORY_REPOSITORY_H
#define PASTES_MEMORY_REPOSITORY_H

#include "tests/core/entryfixtures.h"
#include "application/historyrepository.h"

class MemoryRepository final : public HistoryRepository
{
public:
	void load(void) override { ++loads; }
	quint64 insert(const HistoryEntry &entry) override
	{
		writes.append(cloneEntry(*entry));
		return ++request;
	}
	void remove(const QByteArray &md5) override { removals.append(md5); }
	void updateIcon(const QByteArray &md5, const QImage &) override { iconUpdates.append(md5); }
	void updateFavorite(const QByteArray &md5, bool favorite, qint64 modified, const FavoriteDetails &details) override
	{
		favoriteUpdates.append(md5);
		for (auto &entry : writes) if (entry->md5 == md5) {
			entry->favorite = favorite; entry->favoriteModified = modified;
			entry->favoriteDetails = details;
		}
	}
	void finishLoad(const QList<HistoryEntry> &entries = {}) { emit loaded(entries); }
	void finishImage(quint64 id, const QByteArray &bytes, int format, qreal ratio = 1)
	{
		emit imageEncoded(id, bytes, format, ratio);
	}
	quint64 save(const HistoryEntry &entry, const HistoryWrite &write) override
	{
		const auto &removed = write.removed;
		const bool deleted = write.deleted;
		const auto key = write.key, account = write.account, record = write.record;
		for (const auto &md5 : removed)
			remove(md5);
		const quint64 request =
			(!deleted && entry) ? insert(entry) : (quint64(1) << 63) + ++m_commitRequest;
		QString error;
		const auto document = entry && write.publish
			? SyncContent::document(SyncContent::snapshot(*entry), deleted, key, &error)
			: QByteArray();
		if (!document.isEmpty()) m_events[SyncContent::digest(document)] = document;
		if (!record.isEmpty()) m_applied[account].insert(record);
		QTimer::singleShot(
			0, this, [this, request, document] { emit committed(request, document); });
		return request;
	}
	void loadOutbox(const QString &account, quint64 token, const QSet<QString> &known) override
	{
		QList<SyncContent::JournalRecord> values;
		for (auto it = m_events.cbegin(); it != m_events.cend(); ++it)
			values.append({it.key(), known.contains(it.key()) ? QByteArray() : it.value(),
				!m_sent[account].contains(it.key())});
		QTimer::singleShot(0, this, [this, account, token, values] {
			emit appliedLoaded(account, token, m_applied[account].values());
			emit outboxLoaded(account, token, values);
		});
	}
	void markPublished(const QString &account, const QString &id) override
	{
		m_sent[account].insert(id);
		QTimer::singleShot(0, this, [this, account, id] { emit published(account, id); });
	}

	quint64 m_commitRequest = 0;
	QHash<QString, QByteArray> m_events;
	QHash<QString, QSet<QString>> m_sent, m_applied;
	int loads = 0;
	quint64 request = 0;
	QList<HistoryEntry> writes;
	QList<QByteArray> removals;
	QList<QByteArray> iconUpdates;
	QList<QByteArray> favoriteUpdates;
};

#endif
