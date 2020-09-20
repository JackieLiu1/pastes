#ifndef DATABASE_H
#define DATABASE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QObject>
#include <QMutex>
#include <QDebug>

#include "pasteitem.h"

#define DEBUG() qDebug()<<__FILE__<<__func__<<__LINE__

class Database : public QObject
{
	Q_OBJECT
public:
	explicit Database(QObject *parent = nullptr);
	~Database();
	bool isTableExist(void);
	void createTable(void);
	void insertPasteItem(ItemData *itemData);
	QList<ItemData *> loadData(void);
	void delelePasteItem(QByteArray);

private:
	static QByteArray convertImage2Array(QImage image);

private:
	QSqlDatabase	m_db;
};

#endif // DATABASE_H
