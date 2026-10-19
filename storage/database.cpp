#include "storage/database.h"
#include "core/clipboarddata.h"
#include "core/sourceicon.h"
#include "core/historypolicy.h"

#include <QBuffer>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <utility>

namespace {
const char entrySelect[] =
	"select item.*, coalesce(favorite.selected, 0) as selected, coalesce(favorite.modified, 0) as modified, favorite.name, favorite.name_modified, favorite.position, favorite.position_modified, entry_meta.copied_ms, entry_meta.events, entry_meta.content_key from item left join favorite on item.md5 = favorite.md5 left join entry_meta on item.md5=entry_meta.md5";

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
		for (const auto &sql :
			{"create table if not exists entry_meta(md5 blob primary key, copied_ms integer not null, events blob not null, content_key text not null)",
				"create table if not exists event_clock(id integer primary key, device text not null, version blob not null)",
				"create table if not exists sync_events(id text primary key, document blob not null, content_key text not null, events blob not null, has_icon integer not null, expires integer not null)",
				"create table if not exists sync_sent(account text not null,id text not null,primary key(account,id))",
				"create table if not exists sync_applied(account text not null,content_key text not null,id text not null,primary key(account,content_key))"})
			if (!query.exec(sql)) report(query.lastError());
		if (query.exec("select device,version from event_clock where id=1") && query.next()) {
			m_device = query.value(0).toString();
			m_clock =
				EventVersion::parse(QJsonDocument::fromJson(query.value(1).toByteArray()).object());
		} else {
			query.prepare("insert into event_clock values(1,?,?)");
			query.addBindValue(m_device);
			query.addBindValue(SyncContent::bytes(m_clock.json()));
			if (!query.exec()) report(query.lastError());
		}
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
		emit clockLoaded(m_device, m_clock);
		QList<StoredEntry> entries;
		QSqlQuery query(m_db);
		if (!query.exec(
				QString::fromLatin1(entrySelect) + " order by item.time asc, item.id asc")) {
			report(query.lastError());
			emit loaded(entries);
			return;
		}
		while (query.next()) {
			entries.prepend(readEntry(query));
		}
		emit loaded(entries);
	}

	void insert(StoredEntry entry, quint64 request)
	{
		HistoryWrite write;
		write.publish = false;
		save(entry, write, request);
	}
	void save(StoredEntry entry, HistoryWrite write, quint64 request)
	{
		auto removed = write.removed;
		bool deleted = write.deleted;
		const bool compress =
			entry.encodedImage.isEmpty() && ClipboardData::canCompressImage(entry.image);
		if (entry.hasImage && entry.encodedImage.isEmpty())
			entry.encodedImage = encodeImage(entry.image);
		QString encodingError;
		auto document = write.publish
			? SyncContent::document(entry.portable, deleted, write.key, &encodingError)
			: QByteArray();
		QString contentKey = entry.contentKey;
		EventVersion inputCopy = entry.events.copy;
		// Reserve the writer before reading the current state: upgrading a
		// deferred read transaction can fail immediately under another writer.
		QSqlQuery query(m_db);
		if (!query.exec("begin immediate")) {
			failCommit(request);
			return;
		}
		if (!document.isEmpty()) {
			QString error;
			const auto candidate = SyncContent::inspect(document, &error);
			inputCopy = candidate.events.copy;
			contentKey = candidate.key;
			QList<SyncContent::Record> records{candidate};
			QHash<QString, QByteArray> payloads{{candidate.id, document}};
			query.prepare("select document from sync_events where content_key=?");
			query.addBindValue(contentKey);
			if (!query.exec()) {
				m_db.rollback();
				failCommit(request);
				return;
			}
			while (query.next()) {
				const auto bytes = query.value(0).toByteArray();
				auto record = SyncContent::inspect(bytes, &error);
				if (!error.isEmpty()) break;
				if (!payloads.contains(record.id)) {
					records.append(record);
					payloads[record.id] = bytes;
				}
			}
			if (!error.isEmpty()) {
				m_db.rollback();
				failCommit(request);
				return;
			}
			document = SyncContent::reduce(
				records, [&payloads](const QString &id) { return payloads.value(id); }, &error);
			document = SyncContent::expire(document, QDateTime::currentMSecsSinceEpoch(), &error);
			const auto result = SyncContent::inspect(document, &error);
			if (!error.isEmpty()) {
				m_db.rollback();
				failCommit(request);
				return;
			}
			deleted = result.deleted;
			StoredEntry existing;
			query.prepare(QString::fromLatin1(entrySelect) +
				" where entry_meta.content_key=? order by item.id desc");
			query.addBindValue(contentKey);
			if (!query.exec()) {
				m_db.rollback();
				failCommit(request);
				return;
			}
			while (query.next()) {
				removed.append(query.value("md5").toByteArray());
				if (!deleted && inputCopy != result.events.copy) {
					auto value = readEntry(query);
					if (value.events.copy == result.events.copy) existing = value;
				}
			}
			if (!deleted && inputCopy != result.events.copy) {
				if (!existing.md5.isEmpty())
					entry = existing;
				else {
					auto value =
						SyncContent::decode(result.document.value("content").toObject(), &error);
					if (!error.isEmpty()) {
						m_db.rollback();
						failCommit(request);
						return;
					}
					entry = StoredEntry::fromPortable(value);
				}
			}
			entry.events = result.events;
			entry.contentKey = contentKey;
			entry.time = QDateTime::fromMSecsSinceEpoch(result.copied);
			entry.favorite = result.favorite;
			entry.favoriteModified = result.favoriteModified;
			entry.favoriteDetails = result.favoriteDetails;
			entry.icon = SourceIcon::bounded(QImage::fromData(QByteArray::fromBase64(
				result.document.value("content").toObject().value("icon").toString().toLatin1())));
		}
		if (write.publish && !entry.md5.isEmpty() && !removed.contains(entry.md5))
			removed.append(entry.md5);
		bool ok = true;
		for (const auto &md5 : removed)
			for (const auto &table : {"item", "data", "favorite", "entry_meta"}) {
				query.prepare(
					QStringLiteral("delete from %1 where md5=?").arg(QLatin1String(table)));
				query.addBindValue(md5);
				ok = query.exec() && ok;
			}
		if (!deleted) ok = writeEntry(entry) && ok;
		const auto clock = qMax(m_clock, entry.events.latest());
		query.prepare("update event_clock set version=? where id=1");
		query.addBindValue(SyncContent::bytes(clock.json()));
		ok = query.exec() && ok;
		if (!document.isEmpty()) {
			query.prepare("insert or ignore into sync_events values(?,?,?,?,?,?)");
			query.addBindValue(SyncContent::digest(document));
			query.addBindValue(document);
			query.addBindValue(contentKey);
			query.addBindValue(SyncContent::bytes(entry.events.json()));
			query.addBindValue(!entry.icon.isNull());
			const qint64 expires = entry.favorite || entry.favoriteModified
				? 0
				: (deleted ? entry.events.erasedAt + 60LL * 86400000
						   : entry.time.toMSecsSinceEpoch() +
							  qint64(HistoryPolicy::retentionDays) * 86400000);
			query.addBindValue(expires);
			ok = query.exec() && ok;
			ok = compact(contentKey, SyncContent::digest(document)) && ok;
		}
		if (!write.record.isEmpty()) {
			query.prepare("insert or replace into sync_applied values(?,?,?)");
			query.addBindValue(write.account);
			query.addBindValue(contentKey);
			query.addBindValue(write.record);
			ok = query.exec() && ok;
		}
		if (!ok || !m_db.commit()) {
			m_db.rollback();
			failCommit(request);
			return;
		}
		m_clock = clock;
		if (compress && !deleted && entry.events.copy == inputCopy && !entry.encodedImage.isEmpty())
			emit imageEncoded(
				request, entry.encodedImage, entry.image.format(), entry.image.devicePixelRatio());
		if (!encodingError.isEmpty()) emit syncWarning(encodingError);
		entry.portable = {};
		if (deleted) {
			entry.formats.clear();
			entry.image = {};
			entry.encodedImage.clear();
		}
		emit projected(request, entry, deleted);
		emit committed(request, document);
	}

	void outbox(QString account, quint64 token, QSet<QString> known)
	{
		QList<SyncContent::JournalRecord> result;
		QSqlQuery query(m_db);
		query.prepare("delete from sync_events where expires>0 and expires<=?");
		query.addBindValue(QDateTime::currentMSecsSinceEpoch());
		if (!query.exec()) {
			emit publishFailed(query.lastError().text());
			return;
		}
		if (!query.exec("delete from sync_sent where id not in(select id from sync_events)")) {
			emit publishFailed(query.lastError().text());
			return;
		}
		query.prepare(
			"select sync_events.id, sync_sent.id is null from sync_events left join sync_sent on sync_events.id=sync_sent.id and sync_sent.account=?");
		query.addBindValue(account);
		if (!query.exec()) {
			emit publishFailed(query.lastError().text());
			return;
		}
		while (query.next()) {
			const auto id = query.value(0).toString();
			QByteArray document;
			if (!known.contains(id)) {
				QSqlQuery payload(m_db);
				payload.prepare("select document from sync_events where id=?");
				payload.addBindValue(id);
				if (!payload.exec() || !payload.next()) {
					emit publishFailed(payload.lastError().text());
					return;
				}
				document = payload.value(0).toByteArray();
			}
			result.append({id, document, query.value(1).toBool()});
		}
		QStringList applied;
		query.prepare("select id from sync_applied where account=?");
		query.addBindValue(account);
		if (!query.exec()) {
			emit publishFailed(query.lastError().text());
			return;
		}
		while (query.next())
			applied.append(query.value(0).toString());
		emit appliedLoaded(account, token, applied);
		emit outboxLoaded(account, token, result);
	}
	void markPublished(QString account, QString id)
	{
		QSqlQuery query(m_db);
		query.prepare("insert or ignore into sync_sent values(?,?)");
		query.addBindValue(account);
		query.addBindValue(id);
		if (!query.exec()) {
			emit publishFailed(query.lastError().text());
			return;
		}
		emit published(account, id);
	}

	void remove(const QByteArray &md5)
	{
		if (!m_db.transaction()) { report(m_db.lastError()); return; }
		QSqlQuery query(m_db);
		for (const QString &table : {QStringLiteral("item"), QStringLiteral("data"),
				 QStringLiteral("favorite"), QStringLiteral("entry_meta")}) {
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
	void clockLoaded(QString device, EventVersion version);
	void projected(quint64 request, StoredEntry entry, bool deleted);
	void committed(quint64 request, QByteArray document);
	void commitFailed(quint64 request, QString message);
	void appliedLoaded(QString account, quint64 token, QStringList ids);
	void outboxLoaded(QString account, quint64 token, QList<SyncContent::JournalRecord> documents);
	void published(QString account, QString id);
	void publishFailed(QString message);
	void syncWarning(QString message);
	void loaded(QList<StoredEntry> entries);
	void imageEncoded(quint64 request, QByteArray encoded, int format, qreal ratio);
	void failed(QString message);

private:
	StoredEntry readEntry(QSqlQuery &query)
	{
		StoredEntry entry;
		entry.md5 = query.value("md5").toByteArray();
		entry.contentKey = query.value("content_key").toString();
		entry.favorite = query.value("selected").toBool();
		entry.favoriteModified = query.value("modified").toLongLong();
		entry.favoriteDetails = {query.value("name").toString(),
			query.value("name_modified").toLongLong(), query.value("position").toLongLong(),
			query.value("position_modified").toLongLong()};
		entry.time = query.value("copied_ms").isNull()
			? QDateTime::fromSecsSinceEpoch(query.value("time").toLongLong())
			: QDateTime::fromMSecsSinceEpoch(query.value("copied_ms").toLongLong());
		entry.events = EventState::parse(
			QJsonDocument::fromJson(query.value("events").toByteArray()).object());
		entry.icon = SourceIcon::bounded(QImage::fromData(query.value("icondata").toByteArray()));
		QSqlQuery formats(m_db);
		formats.prepare("select formats,format_data from data where md5=? order by id asc");
		formats.addBindValue(entry.md5);
		if (!formats.exec()) report(formats.lastError());
		while (formats.next()) {
			const auto type = formats.value(0).toString();
			entry.formats.append({type, formats.value(1).toByteArray()});
			entry.hasImage |= type == QStringLiteral("application/x-qt-image");
		}
		if (entry.hasImage) entry.encodedImage = query.value("imagedata").toByteArray();
		return entry;
	}
	bool compact(const QString &key, const QString &canonical)
	{
		struct Metadata {
			QString id;
			EventState events;
			bool icon;
		};
		QList<Metadata> records;
		QSqlQuery query(m_db);
		query.prepare("select id,events,has_icon from sync_events where content_key=?");
		query.addBindValue(key);
		if (!query.exec()) return false;
		while (query.next())
			records.append({query.value(0).toString(),
				EventState::parse(QJsonDocument::fromJson(query.value(1).toByteArray()).object()),
				query.value(2).toBool()});
		const auto next = std::find_if(records.cbegin(), records.cend(),
			[&](const Metadata &record) { return record.id == canonical; });
		if (next == records.cend()) return false;
		// Only the reducer's result can replace another record. Digest order
		// does not express the field tie-breakers used by the reducer.
		for (const auto &old : records) {
			if (old.id == canonical) continue;
			const auto &a = next->events;
			const auto &b = old.events;
			const bool covers = a.copy >= b.copy && a.erase >= b.erase && a.source >= b.source &&
				a.favorite >= b.favorite && a.name >= b.name && a.position >= b.position &&
				(a.source > b.source || a.sourceRevision >= b.sourceRevision) &&
				(!old.icon || next->icon || a.deleted());
			if (!covers) continue;
			query.prepare("delete from sync_events where id=?");
			query.addBindValue(old.id);
			if (!query.exec()) return false;
		}
		return true;
	}
	void failCommit(quint64 request)
	{
		const QString message = m_db.lastError().isValid()
			? m_db.lastError().text()
			: tr("Could not save clipboard history.");
		emit failed(message);
		emit commitFailed(request, message);
	}
	bool writeEntry(const StoredEntry &entry)
	{
		QSqlQuery query(m_db);
		query.prepare("insert into item(md5,imagedata,icondata,time) values(?,?,?,?)");
		query.addBindValue(entry.md5);
		query.addBindValue(entry.encodedImage);
		query.addBindValue(encodeImage(SourceIcon::bounded(entry.icon)));
		query.addBindValue(entry.time.toSecsSinceEpoch());
		if (!query.exec()) {
			report(query.lastError());
			return false;
		}
		for (const auto &format : entry.formats) {
			query.prepare("insert into data(md5,formats,format_data) values(?,?,?)");
			query.addBindValue(entry.md5);
			query.addBindValue(format.first);
			query.addBindValue(format.second);
			if (!query.exec()) {
				report(query.lastError());
				return false;
			}
		}
		query.prepare("insert or replace into entry_meta values(?,?,?,?)");
		query.addBindValue(entry.md5);
		query.addBindValue(entry.time.toMSecsSinceEpoch());
		query.addBindValue(SyncContent::bytes(entry.events.json()));
		query.addBindValue(entry.contentKey.isNull() ? QStringLiteral("") : entry.contentKey);
		if (!query.exec()) {
			report(query.lastError());
			return false;
		}
		return !(entry.favorite || entry.favoriteModified) ||
			writeFavorite(entry.md5, entry.favorite, entry.favoriteModified, entry.favoriteDetails);
	}
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
	QString m_device = QUuid::createUuid().toString(QUuid::WithoutBraces);
	EventVersion m_clock;
	QString m_path;
	QString m_connection;
	QSqlDatabase m_db;
};

Database::Database(const QString &path, QObject *parent)
	: HistoryRepository(parent), m_worker(new Worker(path)), m_thread(new QThread(this))
{
	qRegisterMetaType<StoredEntry>();
	qRegisterMetaType<HistoryWrite>();
	qRegisterMetaType<EventVersion>();
	qRegisterMetaType<QList<SyncContent::JournalRecord>>();
	qRegisterMetaType<QSet<QString>>();
	qRegisterMetaType<FavoriteDetails>();
	qRegisterMetaType<QList<StoredEntry>>();
	m_worker->moveToThread(m_thread);
	connect(m_thread, &QThread::started, m_worker, &Worker::open);
	connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	connect(this, &Database::saveRequested, m_worker, &Worker::save, Qt::QueuedConnection);
	connect(this, &Database::outboxRequested, m_worker, &Worker::outbox, Qt::QueuedConnection);
	connect(this, &Database::publishedRequested, m_worker, &Worker::markPublished,
		Qt::QueuedConnection);
	connect(m_worker, &Worker::clockLoaded, this, &HistoryRepository::clockLoaded,
		Qt::QueuedConnection);
	connect(
		m_worker, &Worker::projected, this,
		[this](quint64 request, const StoredEntry &entry, bool deleted) {
			emit projected(request, entry.materialize(), deleted);
		},
		Qt::QueuedConnection);
	connect(
		m_worker, &Worker::committed, this, &HistoryRepository::committed, Qt::QueuedConnection);
	connect(m_worker, &Worker::commitFailed, this, &HistoryRepository::commitFailed,
		Qt::QueuedConnection);
	connect(m_worker, &Worker::appliedLoaded, this, &HistoryRepository::appliedLoaded,
		Qt::QueuedConnection);
	connect(m_worker, &Worker::outboxLoaded, this, &HistoryRepository::outboxLoaded,
		Qt::QueuedConnection);
	connect(
		m_worker, &Worker::published, this, &HistoryRepository::published, Qt::QueuedConnection);
	connect(m_worker, &Worker::publishFailed, this, &HistoryRepository::publishFailed,
		Qt::QueuedConnection);
	connect(m_worker, &Worker::syncWarning, this, &HistoryRepository::syncWarning,
		Qt::QueuedConnection);
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

quint64 Database::save(const HistoryEntry &entry, const HistoryWrite &write)
{
	const auto request = ++m_nextRequest;
	emit saveRequested(StoredEntry::snapshot(*entry), write, request);
	return request;
}
void Database::loadOutbox(const QString &account, quint64 token, const QSet<QString> &known)
{
	emit outboxRequested(account, token, known);
}
void Database::markPublished(const QString &account, const QString &id)
{
	emit publishedRequested(account, id);
}

#include "database.moc"
