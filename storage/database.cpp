#include "storage/database.h"
#include "core/clipboarddata.h"
#include "core/sourceicon.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
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
		/* Each history item looks up its MIME rows by fingerprint. The index
		 * includes the rowid, retaining the existing format insertion order. */
		if (!query.exec("create index if not exists data_md5_idx on data(md5)")) report(query.lastError());
		if (!query.exec("create table if not exists favorite(md5 blob primary key, selected integer not null, modified integer not null)")) report(query.lastError());
		/* Upgrade only the separate metadata table; legacy clipboard tables
		 * and their payloads retain their original schema. */
		const QSqlRecord columns = m_db.record(QStringLiteral("favorite"));
		for (const auto &column : {QStringLiteral("name text not null default ''"),
			QStringLiteral("name_modified integer not null default 0"),
			QStringLiteral("position integer not null default 0"),
			QStringLiteral("position_modified integer not null default 0")}) {
			if (!columns.contains(column.section(' ', 0, 0)) &&
				!query.exec(QStringLiteral("alter table favorite add column %1").arg(column)))
				report(query.lastError());
		}
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
		if (!query.exec("select item.*, coalesce(favorite.selected, 0) as selected, coalesce(favorite.modified, 0) as modified, favorite.name, favorite.name_modified, favorite.position, favorite.position_modified from item left join favorite on item.md5 = favorite.md5 order by item.time asc, item.id asc;")) {
			report(query.lastError());
			emit loaded(entries);
			return;
		}
		while (query.next()) {
			StoredEntry entry;
			entry.md5 = query.value("md5").toByteArray();
			entry.favorite = query.value("selected").toBool();
			entry.favoriteModified = query.value("modified").toLongLong();
			entry.favoriteDetails = {query.value("name").toString(), query.value("name_modified").toLongLong(),
				query.value("position").toLongLong(), query.value("position_modified").toLongLong()};
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
		if (entry.favorite || entry.favoriteModified) {
			if (!writeFavorite(entry.md5, entry.favorite, entry.favoriteModified, entry.favoriteDetails)) { m_db.rollback(); return; }
		}
		if (!m_db.commit()) { report(m_db.lastError()); m_db.rollback(); return; }
		if (compress && !entry.encodedImage.isEmpty())
			emit imageEncoded(request, entry.encodedImage, entry.image.format(), entry.image.devicePixelRatio());
	}

	void remove(const QByteArray &md5)
	{
		if (!m_db.transaction()) { report(m_db.lastError()); return; }
		QSqlQuery query(m_db);
		for (const QString &table : {QStringLiteral("item"), QStringLiteral("data"), QStringLiteral("favorite")}) {
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

	void updateFavorite(const QByteArray &md5, bool favorite, qint64 modified, const FavoriteDetails &details)
	{
		writeFavorite(md5, favorite, modified, details);
	}

signals:
	void loaded(QList<StoredEntry> entries);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);
	void failed(QString message);

private:
	bool writeFavorite(const QByteArray &md5, bool favorite, qint64 modified, const FavoriteDetails &details)
	{
		QSqlQuery query(m_db);
		query.prepare("insert or replace into favorite(md5, selected, modified, name, name_modified, position, position_modified) values (:md5, :selected, :modified, :name, :name_modified, :position, :position_modified);");
		query.bindValue(":md5", md5);
		query.bindValue(":selected", favorite);
		query.bindValue(":modified", modified);
		query.bindValue(":name", details.name.isNull() ? QStringLiteral("") : details.name);
		query.bindValue(":name_modified", details.nameModified);
		query.bindValue(":position", details.position);
		query.bindValue(":position_modified", details.positionModified);
		if (query.exec()) return true;
		report(query.lastError());
		return false;
	}
	void report(const QSqlError &error) { emit failed(error.text()); }
	QString m_path;
	QString m_connection;
	QSqlDatabase m_db;
};

Database::Database(const QString &path, QObject *parent)
	: HistoryRepository(parent), m_worker(new Worker(path)), m_thread(new QThread(this))
{
	qRegisterMetaType<StoredEntry>();
	qRegisterMetaType<FavoriteDetails>();
	qRegisterMetaType<QList<StoredEntry>>();
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, &Worker::open);
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(this, &Database::loadRequested, m_worker, &Worker::load, Qt::QueuedConnection);
	connect(this, &Database::insertRequested, m_worker, &Worker::insert, Qt::QueuedConnection);
	connect(this, &Database::removeRequested, m_worker, &Worker::remove, Qt::QueuedConnection);
	connect(this, &Database::updateIconRequested, m_worker, &Worker::updateIcon, Qt::QueuedConnection);
	connect(this, &Database::updateFavoriteRequested, m_worker, &Worker::updateFavorite, Qt::QueuedConnection);
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

void Database::updateFavorite(const QByteArray &md5, bool favorite, qint64 modified, const FavoriteDetails &details)
{
	emit updateFavoriteRequested(md5, favorite, modified, details);
}

#include "database.moc"
