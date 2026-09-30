#include "storage/database.h"
#include "core/clipboarddata.h"
#include "core/sourceicon.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <utility>

namespace {
QByteArray encodeImage(const QImage &image)
{
	if (image.isNull()) return QByteArray();
	QByteArray bytes;
	QBuffer buffer(&bytes);
	if (buffer.open(QIODevice::WriteOnly)) image.save(&buffer, "png");
	return bytes;
}
}

class Database::Worker final : public QObject
{
	Q_OBJECT
public:
	explicit Worker(QString path) : m_path(std::move(path)),
		m_connection(QStringLiteral("pastes-%1").arg(QUuid::createUuid().toString())) {}

public slots:
	void open(void)
	{
		if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
			emit failed(QStringLiteral("Cannot create database directory: %1").arg(m_path));
			return;
		}
		m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
		m_db.setDatabaseName(m_path);
		if (!m_db.open()) { report(m_db.lastError()); return; }
		QSqlQuery query(m_db);
		if (!query.exec("create table if not exists item(id integer primary key autoincrement, md5 blob, imagedata blob, icondata blob, time integer)")) report(query.lastError());
		if (!query.exec("create table if not exists data(id integer primary key autoincrement, md5 blob, formats text, format_data blob)")) report(query.lastError());
	}

	void close(void)
	{
		m_db.close();
		m_db = QSqlDatabase();
		QSqlDatabase::removeDatabase(m_connection);
	}

	void load(void)
	{
		QList<StoredEntry> entries;
		QSqlQuery query(m_db);
		if (!query.exec("select * from item order by time asc, id asc;")) {
			report(query.lastError());
			emit loaded(entries);
			return;
		}
		while (query.next()) {
			StoredEntry entry;
			entry.md5 = query.value("md5").toByteArray();
			entry.time = QDateTime::fromSecsSinceEpoch(query.value("time").toLongLong());
			entry.icon = SourceIcon::bounded(QImage::fromData(query.value("icondata").toByteArray()));
			QSqlQuery formats(m_db);
			formats.prepare("select formats, format_data from data where md5 = :md5 order by id asc;");
			formats.bindValue(":md5", entry.md5);
			if (!formats.exec()) report(formats.lastError());
			while (formats.next()) {
				const QString type = formats.value(0).toString();
				entry.formats.append({type, formats.value(1).toByteArray()});
				entry.hasImage |= type == QStringLiteral("application/x-qt-image");
			}
			if (entry.hasImage) entry.encodedImage = query.value("imagedata").toByteArray();
			entries.prepend(entry);
		}
		emit loaded(entries);
	}

	void insert(StoredEntry entry, quint64 request)
	{
		const bool compress = entry.encodedImage.isEmpty() && ClipboardData::canCompressImage(entry.image);
		if (entry.hasImage && entry.encodedImage.isEmpty()) entry.encodedImage = encodeImage(entry.image);
		if (!m_db.transaction()) { report(m_db.lastError()); return; }
		QSqlQuery query(m_db);
		query.prepare("insert into item (md5, imagedata, icondata, time) values (:md5, :image, :icon, :time);");
		query.bindValue(":md5", entry.md5);
		query.bindValue(":image", entry.encodedImage);
		query.bindValue(":icon", encodeImage(SourceIcon::bounded(entry.icon)));
		query.bindValue(":time", entry.time.toSecsSinceEpoch());
		if (!query.exec()) { report(query.lastError()); m_db.rollback(); return; }
		for (const auto &format : entry.formats) {
			query.prepare("insert into data (md5, formats, format_data) values (:md5, :format, :data);");
			query.bindValue(":md5", entry.md5);
			query.bindValue(":format", format.first);
			query.bindValue(":data", format.second);
			if (!query.exec()) { report(query.lastError()); m_db.rollback(); return; }
		}
		if (!m_db.commit()) { report(m_db.lastError()); m_db.rollback(); return; }
		if (compress && !entry.encodedImage.isEmpty())
			emit imageEncoded(request, entry.encodedImage, entry.image.format(), entry.image.devicePixelRatio());
	}

	void remove(const QByteArray &md5)
	{
		if (!m_db.transaction()) { report(m_db.lastError()); return; }
		QSqlQuery query(m_db);
		for (const QString &table : {QStringLiteral("item"), QStringLiteral("data")}) {
			query.prepare(QStringLiteral("delete from %1 where md5 = :md5;").arg(table));
			query.bindValue(":md5", md5);
			if (!query.exec()) { report(query.lastError()); m_db.rollback(); return; }
		}
		if (!m_db.commit()) { report(m_db.lastError()); m_db.rollback(); }
	}

	void updateIcon(const QByteArray &md5, const QImage &icon)
	{
		QSqlQuery query(m_db);
		query.prepare("update item set icondata = :icon where md5 = :md5;");
		query.bindValue(":icon", encodeImage(SourceIcon::bounded(icon)));
		query.bindValue(":md5", md5);
		if (!query.exec()) report(query.lastError());
	}

signals:
	void loaded(QList<StoredEntry> entries);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);
	void failed(QString message);

private:
	void report(const QSqlError &error) { emit failed(error.text()); }
	QString m_path;
	QString m_connection;
	QSqlDatabase m_db;
};

Database::Database(const QString &path, QObject *parent)
	: HistoryRepository(parent), m_worker(new Worker(path)), m_thread(new QThread(this))
{
	qRegisterMetaType<StoredEntry>();
	qRegisterMetaType<QList<StoredEntry>>();
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, &Worker::open);
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(this, &Database::loadRequested, m_worker, &Worker::load, Qt::QueuedConnection);
	connect(this, &Database::insertRequested, m_worker, &Worker::insert, Qt::QueuedConnection);
	connect(this, &Database::removeRequested, m_worker, &Worker::remove, Qt::QueuedConnection);
	connect(this, &Database::updateIconRequested, m_worker, &Worker::updateIcon, Qt::QueuedConnection);
	connect(m_worker, &Worker::loaded, this, [this](const QList<StoredEntry> &stored) {
		QList<HistoryEntry> entries;
		entries.reserve(stored.size());
		for (const StoredEntry &entry : stored) entries.append(entry.materialize());
		emit loaded(entries);
	}, Qt::QueuedConnection);
	connect(m_worker, &Worker::imageEncoded, this, &HistoryRepository::imageEncoded, Qt::QueuedConnection);
	connect(m_worker, &Worker::failed, this, &HistoryRepository::failed, Qt::QueuedConnection);
	m_thread->start();
}

Database::~Database()
{
	/* A FIFO barrier drains all earlier requests before closing SQL. */
	QMetaObject::invokeMethod(m_worker, &Worker::close, Qt::BlockingQueuedConnection);
	m_thread->quit();
	m_thread->wait();
}

void Database::load(void) { emit loadRequested(); }

quint64 Database::insert(const HistoryEntry &entry)
{
	const quint64 request = ++m_nextRequest;
	emit insertRequested(StoredEntry::snapshot(*entry), request);
	return request;
}

void Database::remove(const QByteArray &md5) { emit removeRequested(md5); }
void Database::updateIcon(const QByteArray &md5, const QImage &icon) { emit updateIconRequested(md5, icon); }

#include "database.moc"
