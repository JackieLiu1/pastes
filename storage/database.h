#ifndef PASTES_DATABASE_H
#define PASTES_DATABASE_H

#include "application/historyrepository.h"
#include "storage/storedentry.h"

class QThread;

/* SQLite adapter. Its worker alone owns SQL connections. Destruction drains
 * queued commands and closes the connection on that same worker thread. */
class Database final : public HistoryRepository
{
	Q_OBJECT
public:
	explicit Database(const QString &path, QObject *parent = nullptr);
	~Database() override;
	void load(void) override;
	quint64 insert(const HistoryEntry &entry) override;
	void remove(const QByteArray &md5) override;
	void updateIcon(const QByteArray &md5, const QImage &icon) override;
	void updateFavorite(const QByteArray &md5, bool favorite, qint64 modified) override;

signals:
	void loadRequested(void);
	void insertRequested(StoredEntry entry, quint64 request);
	void removeRequested(QByteArray md5);
	void updateIconRequested(QByteArray md5, QImage icon);
	void updateFavoriteRequested(QByteArray md5, bool favorite, qint64 modified);

private:
	class Worker;
	Worker *m_worker;
	QThread *m_thread;
	quint64 m_nextRequest = 0;
};

#endif
