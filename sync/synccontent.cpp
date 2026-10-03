#include "sync/synccontent.h"
#include "core/clipboarddata.h"
#include "core/clipboardcontent.h"
#include "core/historypolicy.h"
#include "core/sourceicon.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace SyncContent {
namespace {
QByteArray pngBytes(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	return image.save(&buffer, "PNG") ? bytes : QByteArray();
}
QImage readImage(const QByteArray &bytes, qint64 maxPixels)
{
	QBuffer buffer;
	buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer, "PNG");
	const QSize size = reader.size();
	if (!size.isValid() || qint64(size.width())*size.height() > maxPixels) return {};
	return reader.read();
}
}
Snapshot snapshot(const ItemData &entry)
{
	Snapshot value;
	const auto &mime = *entry.mimeData;
	value.md5 = entry.md5; value.time = entry.time; value.icon = entry.icon;
	value.favorite = entry.favorite; value.favoriteModified = entry.favoriteModified;
	value.favoriteDetails = entry.favoriteDetails;
	if (ClipboardContent::prefersImage(mime)) {
		value.kind = "image"; value.png = ClipboardData::storedImage(&mime);
		if (value.png.isEmpty()) value.image = qvariant_cast<QImage>(mime.imageData());
	} else if (mime.hasUrls() && !mime.urls().isEmpty()) {
		for (const QUrl &url : mime.urls()) {
			if (url.isLocalFile() || (url.scheme() != "https" && url.scheme() != "http")) return {};
			value.urls.append(QString::fromUtf8(url.toEncoded()));
		}
		value.kind = "links";
	} else if (mime.hasHtml() && !mime.text().trimmed().isEmpty()) {
		value.kind = "text"; value.text = mime.text(); value.html = mime.html();
	} else if (mime.hasImage()) {
		value.kind = "image"; value.png = ClipboardData::storedImage(&mime);
		if (value.png.isEmpty()) value.image = qvariant_cast<QImage>(mime.imageData());
	} else if (!mime.text().trimmed().isEmpty()) {
		value.kind = "text"; value.text = mime.text();
	}
	return value;
}
HistoryEntry materialize(const Snapshot &value)
{
	auto entry = HistoryEntry::create();
	entry->md5 = value.md5; entry->time = value.time; entry->icon = value.icon;
	entry->favorite = value.favorite; entry->favoriteModified = value.favoriteModified;
	entry->favoriteDetails = value.favoriteDetails;
	entry->mimeData = std::make_unique<QMimeData>();
	if (value.kind == "image") {
		entry->mimeData = ClipboardData::withStoredImage(entry->mimeData.get(),
			value.png, QImage::Format_Invalid);
	} else if (value.kind == "links") {
		QList<QUrl> urls;
		for (const auto &url : value.urls) urls.append(QUrl(url));
		entry->mimeData->setUrls(urls);
	} else {
		entry->mimeData->setText(value.text);
		if (!value.html.isEmpty()) entry->mimeData->setHtml(value.html);
	}
	return entry;
}
QJsonObject encode(Snapshot value, QString *error)
{
	QJsonObject payload{{"kind", value.kind}};
	if (value.kind == "image") {
		if (value.image.isNull()) value.image = readImage(value.png, maxImagePixels);
		if (value.image.isNull() || qint64(value.image.width())*value.image.height() > maxImagePixels) {
			*error = QObject::tr("An image is too large to sync (maximum 20 megapixels)."); return {};
		}
		// Normalize the wire image for stable identity on all platforms.
		value.png = pngBytes(value.image.convertToFormat(QImage::Format_RGBA8888));
		if (value.png.isEmpty() || value.png.size() > maxContentBytes) {
			*error = QObject::tr("An item is too large to sync (maximum 20 MB)."); return {};
		}
		payload["png"] = QString::fromLatin1(value.png.toBase64());
	} else if (value.kind == "text") {
		if (value.text.toUtf8().size()+value.html.toUtf8().size() > maxContentBytes) {
			*error = QObject::tr("An item is too large to sync (maximum 20 MB)."); return {};
		}
		payload["text"] = value.text; payload["html"] = value.html;
	} else if (value.kind == "links") {
		payload["urls"] = QJsonArray::fromStringList(value.urls);
	} else return {};
	if (!value.icon.isNull()) payload["icon"] = QString::fromLatin1(pngBytes(
		SourceIcon::bounded(value.icon)).toBase64());
	return payload;
}
Snapshot decode(const QJsonObject &payload, QString *error)
{
	Snapshot value;
	value.kind = payload.value("kind").toString();
	if (value.kind == "text") {
		value.text = payload.value("text").toString(); value.html = payload.value("html").toString();
		if (value.text.trimmed().isEmpty() || value.text.toUtf8().size()+value.html.toUtf8().size() > maxContentBytes) {
			*error = QObject::tr("Invalid text in sync data."); return {};
		}
		value.md5 = QCryptographicHash::hash(value.text.trimmed().toLocal8Bit(), QCryptographicHash::Md5);
	} else if (value.kind == "links") {
		const auto urls = payload.value("urls").toArray();
		if (urls.isEmpty() || urls.size() > 1000) { *error = QObject::tr("Invalid links in sync data."); return {}; }
		QCryptographicHash hash(QCryptographicHash::Md5);
		for (const auto &item : urls) {
			const QUrl url(item.toString());
			if (!url.isValid() || url.host().isEmpty() || (url.scheme() != "https" && url.scheme() != "http")) {
				*error = QObject::tr("Invalid links in sync data."); return {};
			}
			value.urls.append(QString::fromUtf8(url.toEncoded())); hash.addData(url.toEncoded());
		}
		value.md5 = hash.result();
	} else if (value.kind == "image") {
		value.png = QByteArray::fromBase64(payload.value("png").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		if (value.png.size() <= maxContentBytes) value.image = readImage(value.png, maxImagePixels);
		if (value.image.isNull()) { *error = QObject::tr("Invalid or oversized image in sync data."); return {}; }
		value.md5 = ClipboardContent::imageFingerprint(value.image);
	} else { *error = QObject::tr("Unsupported sync content."); return {}; }
	const QByteArray icon = QByteArray::fromBase64(payload.value("icon").toString().toLatin1());
	if (icon.size() < 128*1024) value.icon = SourceIcon::bounded(
		readImage(icon, SourceIcon::maxPixels*SourceIcon::maxPixels));
	return value;
}
QString digest(const QByteArray &value)
{
	return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
QString key(const Snapshot &value)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(value.kind.toUtf8()); hash.addData(QByteArray(1, '\0'));
	if (value.kind == "text") hash.addData(value.text.trimmed().toUtf8());
	else if (value.kind == "links") hash.addData(QJsonDocument(QJsonArray::fromStringList(value.urls)).toJson(QJsonDocument::Compact));
	else {
		const QImage image = value.image.convertToFormat(QImage::Format_RGBA8888);
		hash.addData(QByteArray::number(image.width())+"x"+QByteArray::number(image.height())+":");
		for (int row = 0; row < image.height(); ++row)
			hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(row)), image.width()*4));
	}
	return QString::fromLatin1(hash.result().toHex());
}
QByteArray bytes(const QJsonObject &document) { return QJsonDocument(document).toJson(QJsonDocument::Compact); }
void writeFavoriteDetails(QJsonObject &document, const FavoriteDetails &details)
{
	if (!details.nameModified && !details.positionModified) return;
	document["version"] = 3;
	document["favoriteName"] = details.name;
	document["favoriteNameModified"] = details.nameModified;
	document["favoritePosition"] = details.position;
	document["favoritePositionModified"] = details.positionModified;
}

Record parse(const QByteArray &data, QString *error)
{
	Record record;
	if (data.size() > maxDocumentBytes) { *error = QObject::tr("Sync data exceeds the size limit."); return {}; }
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(data, &parseError);
	const auto object = json.object();
	record.key = object.value("key").toString(); record.modified = object.value("modified").toInteger();
	record.copied = object.value("copied").toInteger();
	record.deleted = object.value("deleted").toBool(); record.document = object; record.id = digest(data);
	record.favorite = object.value("favorite").toBool();
	record.favoriteModified = object.value("favoriteModified").toInteger();
	const auto keyBytes = record.key.toLatin1();
	const int version = object.value("version").toInt();
	if (version == 3) {
		record.favoriteDetails = {object.value("favoriteName").toString(),
			object.value("favoriteNameModified").toInteger(-1),
			object.value("favoritePosition").toInteger(-1),
			object.value("favoritePositionModified").toInteger(-1)};
	}
	const auto &details = record.favoriteDetails;
	const bool invalidDetails = version == 3 &&
		(!object.value("favoriteName").isString() || details.name.size() > FavoriteDetails::maxNameLength ||
		 details.name != details.name.simplified() || details.nameModified < 0 || details.nameModified > record.modified ||
		 (!details.nameModified && !details.name.isEmpty()) || details.position < 0 ||
		 details.position > FavoriteDetails::maxPosition || details.positionModified < 0 || details.positionModified > record.modified ||
		 (!details.positionModified && details.position != 0));
	if (parseError.error != QJsonParseError::NoError || (version != 1 && version != 2 && version != 3) || invalidDetails ||
		record.key.size() != 64 || QByteArray::fromHex(keyBytes).toHex() != keyBytes ||
		record.modified <= 0 || record.modified > QDateTime::currentMSecsSinceEpoch()+5*60*1000 ||
		(object.contains("favorite") && !object.value("favorite").isBool()) ||
		record.favoriteModified < 0 || record.favoriteModified > record.modified ||
		(record.favorite && (record.deleted || !record.favoriteModified))) {
		*error = QObject::tr("Invalid sync record or incompatible version."); return {};
	}
	if (!record.deleted) {
		const qint64 copied = object.value("copied").toInteger();
		const auto content = decode(object.value("content").toObject(), error);
		if (!error->isEmpty()) return {};
		if (copied <= 0 || copied > record.modified || key(content) != record.key) {
			*error = QObject::tr("The sync record failed its content check."); return {};
		}
	}
	return record;
}
bool expired(const Record &record, qint64 now)
{
	/* Keep favorite removal markers so an offline device cannot revive an
	 * old favorite after ordinary deletion markers have aged out. */
	if (record.deleted ? record.favoriteModified > 0 : record.favorite) return false;
	const qint64 age = now-(record.deleted ? record.modified : record.copied);
	return age >= qint64(record.deleted ? 60 : HistoryPolicy::retentionDays)*86400000;
}
}
