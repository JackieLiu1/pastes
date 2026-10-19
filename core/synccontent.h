#ifndef PASTES_SYNCCONTENT_H
#define PASTES_SYNCCONTENT_H
#include "core/itemdata.h"
#include <QJsonObject>
#include <functional>

namespace SyncContent
{
constexpr qint64 maxContentBytes = 20 * 1024 * 1024;
constexpr qint64 maxDocumentBytes = 30 * 1024 * 1024;
constexpr qint64 maxImagePixels = 20 * 1024 * 1024;

/* Only values cross the worker boundary. Native clipboard formats and file
 * URLs are deliberately absent from the portable representation. */
struct JournalRecord {
	QString id;
	QByteArray document;
	bool pending = true;
};
struct Snapshot {
	QByteArray md5;
	EventState events;
	QDateTime time;
	bool favorite = false;
	qint64 favoriteModified = 0;
	FavoriteDetails favoriteDetails;
	QString kind;
	QString text;
	QString html;
	QStringList urls;
	QByteArray png;
	QImage image;
	QImage icon;
};
struct Record {
	QString key;
	EventState events;
	QString id;
	qint64 modified = 0;
	qint64 copied = 0;
	bool deleted = false;
	bool hasIcon = false;
	qint64 iconCaptured = 0;
	QByteArray iconKey;
	bool favorite = false;
	qint64 favoriteModified = 0;
	FavoriteDetails favoriteDetails;
	QJsonObject document;
};
Snapshot snapshot(const ItemData &entry);
HistoryEntry materialize(const Snapshot &value);
QJsonObject encode(Snapshot value, QString *error);
Snapshot decode(const QJsonObject &payload, QString *error);
QString key(const Snapshot &value);
QByteArray bytes(const QJsonObject &document);
QString digest(const QByteArray &bytes);
QByteArray document(Snapshot value, bool deleted, const QString &knownKey, QString *error);
Snapshot merge(Snapshot target, const Snapshot &source);
Record parse(const QByteArray &bytes, QString *error);
/* Trusted SQLite/derived records: inspect metadata without decoding original pixels. */
Record inspect(const QByteArray &bytes, QString *error);
QByteArray reduce(const QList<Record> &records,
	const std::function<QByteArray(const QString &)> &read, QString *error);
QByteArray expire(QByteArray document, qint64 now, QString *error);
void writeFavoriteDetails(QJsonObject &document, const FavoriteDetails &details);
bool expired(const Record &record, qint64 now);
} // namespace SyncContent
Q_DECLARE_METATYPE(SyncContent::JournalRecord)
Q_DECLARE_METATYPE(QList<SyncContent::JournalRecord>)
#endif
