#ifndef PASTES_SYNCCONTENT_H
#define PASTES_SYNCCONTENT_H
#include "core/itemdata.h"
#include <QJsonObject>

namespace SyncContent {
constexpr qint64 maxContentBytes = 20*1024*1024;
constexpr qint64 maxDocumentBytes = 30*1024*1024;
constexpr qint64 maxImagePixels = 20*1024*1024;

/* Only values cross the worker boundary. Native clipboard formats and file
 * URLs are deliberately absent from the portable representation. */
struct Snapshot {
	QByteArray md5;
	QDateTime time;
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
	QString id;
	qint64 modified = 0;
	qint64 copied = 0;
	bool deleted = false;
	QJsonObject document;
};
Snapshot snapshot(const ItemData &entry);
HistoryEntry materialize(const Snapshot &value);
QJsonObject encode(Snapshot value, QString *error);
Snapshot decode(const QJsonObject &payload, QString *error);
QString key(const Snapshot &value);
QByteArray bytes(const QJsonObject &document);
QString digest(const QByteArray &bytes);
Record parse(const QByteArray &bytes, QString *error);
bool expired(const Record &record, qint64 now);
}
#endif
