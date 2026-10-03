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
