#ifndef PASTES_TESTSUPPORT_H
#define PASTES_TESTSUPPORT_H

#include "application/historyrepository.h"
#include "core/clipboardcontent.h"
#include <QBuffer>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <functional>
#include <stdexcept>

inline void require(bool value, const char *message)
{
	if (!value) throw std::runtime_error(message);
}

inline HistoryEntry textEntry(const QString &text, QDateTime time = QDateTime::currentDateTime())
{
	auto entry = HistoryEntry::create();
	entry->mimeData = std::make_unique<QMimeData>();
	entry->mimeData->setText(text);
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	entry->time = time;
	return entry;
}

inline EventVersion nextVersion(EventVersion previous, const QString &device = "test-peer")
{
	++previous.logical;
	previous.device = device;
	return previous;
}
inline void observeIcon(const HistoryEntry &entry, const QImage &icon, EventVersion version = {})
{
	if (!version.valid())
		version = entry->events.copy.valid()
			? entry->events.copy
			: EventVersion{entry->time.toMSecsSinceEpoch(), 0, "test-source"};
	entry->events.source = version;
	entry->events.sourceCaptured = entry->time.toMSecsSinceEpoch();
	entry->icon = icon;
}

inline QByteArray png(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	require(image.save(&buffer, "PNG"), "PNG encoding failed");
	return bytes;
}

inline void waitUntil(const std::function<bool()> &condition, int timeout = 3000)
{
	if (condition()) return;
	QEventLoop loop;
	QTimer poll;
	poll.setInterval(5);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (condition()) loop.quit(); });
	QTimer::singleShot(timeout, &loop, &QEventLoop::quit);
	poll.start();
	loop.exec();
	require(condition(), "Timed out waiting for asynchronous completion");
}

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

inline int runTest(const char *name, const std::function<void()> &test)
{
	try { test(); qInfo() << "PASS" << name; return 0; }
	catch (const std::exception &error) { qCritical() << "FAIL" << name << error.what(); return 1; }
}

#endif
