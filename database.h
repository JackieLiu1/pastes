#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QObject>
#include <QMutex>

#include "pasteitem.h"

class Database : public QObject
{
	Q_OBJECT
public:
	explicit Database(QObject *parent = nullptr);
	~Database();
	bool isTableExist(void);
	void createTable(void);
	void insertPasteItem(ItemData *itemData);
	void loadData(void);
	void delelePasteItem(ItemData *itemData);

private:
	static QByteArray convertImage2Array(QImage image);

private:
	QSqlDatabase	m_db;

signals:
	void dataLoaded(QList<ItemData *>);
};

#endif // DATABASE_H
