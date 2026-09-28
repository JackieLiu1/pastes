#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QObject>
#include <QThread>

#include "pasteitem.h"

/*
 * All SQL work runs on one dedicated worker thread which owns its own
 * QSqlDatabase connection: Qt SQL connections must only be used on the
 * thread that created them. The public API below only forwards requests
 * to the worker through queued (order preserving) connections.
 *
 * Ownership rules: the caller keeps owning an ItemData until it is handed
 * to deletePasteItem(); the worker frees it after the rows are removed.
 */
class Database : public QObject
{
	Q_OBJECT
public:
	explicit Database(QObject *parent = nullptr);
	~Database();

	void loadData(void);
	quint64 insertPasteItem(ItemData *itemData);
	void updatePasteItemIcon(const QByteArray &md5, const QImage &icon);
	/* Removes the rows from the database and deletes itemData on the worker thread */
	void deletePasteItem(ItemData *itemData);

signals:
	void dataLoaded(QList<ItemData *> list);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);

	/* Internal: forwarded to the worker thread */
	void loadRequested(void);
	void insertRequested(ItemData *itemData, QImage iconImage, quint64 request);
	void updateIconRequested(QByteArray md5, QImage icon);
	void deleteRequested(ItemData *itemData);

private:
	class Worker;
	Worker		*m_worker;
	QThread		*m_thread;
	quint64 m_image_request = 0;
};

#endif // DATABASE_H
