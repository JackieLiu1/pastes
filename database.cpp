#include "database.h"
#include "platform/paths.h"

#include <QApplication>
#include <QSqlError>
#include <QBuffer>
#include <QSqlRecord>
#include <QDateTime>
#include <QDebug>

#define DEBUG() qDebug()<<__FILE__<<__func__<<__LINE__

/*
 * Lives on the worker thread and owns the QSqlDatabase connection.
 * All public slots are invoked from the Database facade through queued
 * connections, so they run serialized in the worker thread's event loop.
 */
class Database::Worker : public QObject
{
	Q_OBJECT

public:
	Worker(const QString &databaseName, QObject *parent = nullptr)
		: QObject(parent), m_databaseName(databaseName) {}

public slots:
	void open(void)
	{
		if (!Platform::prepareDatabaseDirectory(m_databaseName)) {
			qWarning() << "Pastes: unable to create database directory";
			return;
		}
		m_db = QSqlDatabase::addDatabase("QSQLITE", "pastes-worker");
		m_db.setDatabaseName(m_databaseName);
		DEBUG() << m_db.databaseName();

		if (!m_db.open()) {
			DEBUG() << m_db.lastError();
			return;
		}

		QSqlQuery query(m_db);
		if (!query.exec("create table if not exists item(id integer primary key autoincrement, md5 blob, imagedata blob, icondata blob, time integer)"))
			DEBUG() << query.lastError();
		if (!query.exec("create table if not exists data(id integer primary key autoincrement, md5 blob, formats text, format_data blob)"))
			DEBUG() << query.lastError();
	}

	void load(void)
	{
		QList<ItemData *> list;
		QSqlQuery query(m_db);

		/* Undo reinserts an old copy with a new ID. Keep copy time primary;
		 * push_front below reverses this order into newest-first history. */
		if (!query.exec("select * from item order by time asc, id asc;")) {
			DEBUG() << query.lastError();
			emit dataLoaded(list);
			return;
		}

		while (query.next()) {
			ItemData *itemData = new ItemData;
			itemData->md5 = query.value("md5").toByteArray();
			itemData->time = QDateTime::fromSecsSinceEpoch(query.value("time").toUInt());
			itemData->mimeData = new QMimeData;

			QSqlQuery query_data(m_db);
			query_data.prepare("select * from data where md5 = x'" + itemData->md5.toHex() + "'");
			if (!query_data.exec())
				DEBUG() << query_data.lastError();

			while (query_data.next()) {
				QString mimeType = query_data.value("formats").toString();
				QByteArray data = query_data.value("format_data").toByteArray();
				itemData->mimeData->setData(mimeType, data);
			}

			/* Icons are small. Keep original images encoded so loading history
			 * does not retain every full-resolution bitmap in memory. */
			itemData->icon = QImage::fromData(query.value("icondata").toByteArray());
			if (itemData->icon.width() > 64 || itemData->icon.height() > 64)
				itemData->icon = itemData->icon.scaled(QSize(64, 64), Qt::KeepAspectRatio, Qt::SmoothTransformation);

			if (itemData->mimeData->hasImage()) {
				const QByteArray encoded = query.value("imagedata").toByteArray();
				if (!encoded.isEmpty()) {
					QMimeData *mime = ClipboardData::withStoredImage(itemData->mimeData, encoded, QImage::Format_Invalid);
					delete itemData->mimeData;
					itemData->mimeData = mime;
				}
			}

			list.push_front(itemData);
		}

		emit dataLoaded(list);
	}

	void insert(ItemData *itd, const QImage &iconImage, quint64 request)
	{
		QSqlQuery query(m_db);
		QByteArray encoded;
		int imageFormat = QImage::Format_Invalid;
		qreal imageRatio = 1;

		query.prepare("insert into item (md5, imagedata, icondata, time) values (:md5, :imagedata, :icondata, :time);");
		query.bindValue(":md5", itd->md5);
		if (itd->mimeData->hasImage()) {
			encoded = ClipboardData::storedImage(itd->mimeData);
			if (encoded.isEmpty()) {
				const QImage image = qvariant_cast<QImage>(itd->mimeData->imageData());
				encoded = Worker::convertImage2Array(image);
				if (ClipboardData::canCompressImage(image)) imageFormat = image.format();
				imageRatio = image.devicePixelRatio();
			}
			query.bindValue(":imagedata", encoded);
		}
		query.bindValue(":icondata", Worker::convertImage2Array(iconImage));
		query.bindValue(":time", itd->time.toSecsSinceEpoch());

		if (!query.exec())
			DEBUG() << query.lastError();

		for (QString format : itd->mimeData->formats()) {
			QSqlQuery query_data(m_db);
			query_data.prepare("insert into data (md5, formats, format_data) values (:md5, :formats, :format_data);");
			query_data.bindValue(":md5", itd->md5);
			query_data.bindValue(":formats", format);
			query_data.bindValue(":format_data", itd->mimeData->data(format));

			if (!query_data.exec())
				DEBUG() << query_data.lastError();
		}
		/* All reads of the UI-owned MIME object are complete before the
		 * queued receiver can replace it with its compact representation. */
		if (!encoded.isEmpty() && imageFormat != QImage::Format_Invalid)
			emit imageEncoded(request, encoded, imageFormat, imageRatio);
	}

	void updateIcon(const QByteArray &md5, const QImage &icon)
	{
		QSqlQuery query(m_db);
		query.prepare("update item set icondata = :icon where md5 = :md5;");
		query.bindValue(":icon", Worker::convertImage2Array(icon));
		query.bindValue(":md5", md5);
		if (!query.exec())
			DEBUG() << query.lastError();
	}

	void remove(ItemData *itemData)
	{
		QSqlQuery query(m_db);
		QByteArray md5 = itemData->md5;

		query.prepare("delete from item where md5 = x'" + md5.toHex() + "'");
		if (!query.exec())
			DEBUG() << query.lastError();
		query.prepare("delete from data where md5 = x'" + md5.toHex() + "'");
		if (!query.exec())
			DEBUG() << query.lastError();

		delete itemData->mimeData;
		delete itemData;
	}

signals:
	void dataLoaded(QList<ItemData *> list);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);

private:
	static QByteArray convertImage2Array(QImage image)
	{
		QByteArray imagedata;
		QBuffer buffer(&imagedata);

		buffer.open(QIODevice::WriteOnly);
		image.save(&buffer, "png");
		buffer.close();

		return imagedata;
	}

	QString			m_databaseName;
	QSqlDatabase		m_db;
};

Database::Database(QObject *parent) : QObject(parent),
	m_worker(nullptr),
	m_thread(new QThread(this))
{
	qRegisterMetaType<ItemData *>("ItemData *");
	qRegisterMetaType<QList<ItemData *>>("QList<ItemData *>");

	const QString databaseName = Platform::databasePath();
	m_worker = new Worker(databaseName);
	m_worker->moveToThread(m_thread);

	QObject::connect(m_thread, &QThread::started, m_worker, &Worker::open);
	QObject::connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	QObject::connect(this, &Database::loadRequested, m_worker, &Worker::load);
	QObject::connect(this, &Database::insertRequested, m_worker, &Worker::insert);
	QObject::connect(this, &Database::updateIconRequested, m_worker, &Worker::updateIcon);
	QObject::connect(this, &Database::deleteRequested, m_worker, &Worker::remove);
	QObject::connect(m_worker, &Worker::dataLoaded, this, &Database::dataLoaded);
	QObject::connect(m_worker, &Worker::imageEncoded, this, &Database::imageEncoded);

	m_thread->start();
}

Database::~Database()
{
	m_thread->quit();
	m_thread->wait();
}

void Database::loadData(void)
{
	emit loadRequested();
}

quint64 Database::insertPasteItem(ItemData *itemData)
{
	const quint64 request = ++m_image_request;
	emit insertRequested(itemData, itemData->icon, request);
	return request;
}

void Database::updatePasteItemIcon(const QByteArray &md5, const QImage &icon)
{
	emit updateIconRequested(md5, icon);
}

void Database::deletePasteItem(ItemData *itemData)
{
	emit deleteRequested(itemData);
}

#include "database.moc"
