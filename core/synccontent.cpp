#include "core/synccontent.h"
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

namespace SyncContent
{
namespace
{
QByteArray pngBytes(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	return image.save(&buffer, "PNG") ? bytes : QByteArray();
}
QImage readImage(const QByteArray &bytes, qint64 maxPixels)
{
	if (bytes.isEmpty()) return {};
	QBuffer buffer;
	buffer.setData(bytes);
	buffer.open(QIODevice::ReadOnly);
	QImageReader reader(&buffer, "PNG");
	const QSize size = reader.size();
	if (!size.isValid() || qint64(size.width()) * size.height() > maxPixels) return {};
	return reader.read();
}
} // namespace
Snapshot snapshot(const ItemData &entry)
{
	Snapshot value;
	const auto &mime = *entry.mimeData;
	value.events = entry.events;
	value.md5 = entry.md5;
	value.time = entry.time;
	value.icon = entry.icon;
	value.favorite = entry.favorite;
	value.favoriteModified = entry.favoriteModified;
	value.favoriteDetails = entry.favoriteDetails;
	if (ClipboardContent::prefersImage(mime)) {
		value.kind = "image";
		value.png = ClipboardData::storedImage(&mime);
		if (value.png.isEmpty()) value.image = qvariant_cast<QImage>(mime.imageData());
	} else if (mime.hasUrls() && !mime.urls().isEmpty()) {
		for (const QUrl &url : mime.urls()) {
			if (url.isLocalFile() || (url.scheme() != "https" && url.scheme() != "http")) return {};
			value.urls.append(QString::fromUtf8(url.toEncoded()));
		}
		value.kind = "links";
	} else if (mime.hasHtml() && !mime.text().trimmed().isEmpty()) {
		value.kind = "text";
		value.text = mime.text();
		value.html = mime.html();
	} else if (mime.hasImage()) {
		value.kind = "image";
		value.png = ClipboardData::storedImage(&mime);
		if (value.png.isEmpty()) value.image = qvariant_cast<QImage>(mime.imageData());
	} else if (!mime.text().trimmed().isEmpty()) {
		value.kind = "text";
		value.text = mime.text();
	}
	return value;
}
HistoryEntry materialize(const Snapshot &value)
{
	auto entry = HistoryEntry::create();
	entry->events = value.events;
	entry->md5 = value.md5;
	entry->time = value.time;
	entry->icon = value.icon;
	entry->favorite = value.favorite;
	entry->favoriteModified = value.favoriteModified;
	entry->favoriteDetails = value.favoriteDetails;
	entry->mimeData = std::make_unique<QMimeData>();
	if (value.kind == "image") {
		entry->mimeData = ClipboardData::withStoredImage(
			entry->mimeData.get(), value.png, QImage::Format_Invalid);
	} else if (value.kind == "links") {
		QList<QUrl> urls;
		for (const auto &url : value.urls)
			urls.append(QUrl(url));
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
		if (value.image.isNull() ||
			qint64(value.image.width()) * value.image.height() > maxImagePixels) {
			*error = QObject::tr("An image is too large to sync (maximum 20 megapixels).");
			return {};
		}
		// Normalize the wire image for stable identity on all platforms.
		value.png = pngBytes(value.image.convertToFormat(QImage::Format_RGBA8888));
		if (value.png.isEmpty() || value.png.size() > maxContentBytes) {
			*error = QObject::tr("An item is too large to sync (maximum 20 MB).");
			return {};
		}
		payload["png"] = QString::fromLatin1(value.png.toBase64());
	} else if (value.kind == "text") {
		if (value.text.toUtf8().size() + value.html.toUtf8().size() > maxContentBytes) {
			*error = QObject::tr("An item is too large to sync (maximum 20 MB).");
			return {};
		}
		payload["text"] = value.text;
		payload["html"] = value.html;
	} else if (value.kind == "links") {
		payload["urls"] = QJsonArray::fromStringList(value.urls);
	} else
		return {};
	if (!value.icon.isNull())
		payload["icon"] = QString::fromLatin1(pngBytes(SourceIcon::bounded(value.icon)).toBase64());
	return payload;
}
Snapshot decode(const QJsonObject &payload, QString *error)
{
	Snapshot value;
	value.kind = payload.value("kind").toString();
	if (value.kind == "text") {
		value.text = payload.value("text").toString();
		value.html = payload.value("html").toString();
		if (value.text.trimmed().isEmpty() ||
			value.text.toUtf8().size() + value.html.toUtf8().size() > maxContentBytes) {
			*error = QObject::tr("Invalid text in sync data.");
			return {};
		}
		value.md5 =
			QCryptographicHash::hash(value.text.trimmed().toLocal8Bit(), QCryptographicHash::Md5);
	} else if (value.kind == "links") {
		const auto urls = payload.value("urls").toArray();
		if (urls.isEmpty() || urls.size() > 1000) {
			*error = QObject::tr("Invalid links in sync data.");
			return {};
		}
		QCryptographicHash hash(QCryptographicHash::Md5);
		for (const auto &item : urls) {
			const QUrl url(item.toString());
			if (!url.isValid() || url.host().isEmpty() ||
				(url.scheme() != "https" && url.scheme() != "http")) {
				*error = QObject::tr("Invalid links in sync data.");
				return {};
			}
			value.urls.append(QString::fromUtf8(url.toEncoded()));
			hash.addData(url.toEncoded());
		}
		value.md5 = hash.result();
	} else if (value.kind == "image") {
		value.png = QByteArray::fromBase64(
			payload.value("png").toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
		if (value.png.size() <= maxContentBytes) value.image = readImage(value.png, maxImagePixels);
		if (value.image.isNull()) {
			*error = QObject::tr("Invalid or oversized image in sync data.");
			return {};
		}
		value.md5 = ClipboardContent::imageFingerprint(value.image);
	} else {
		*error = QObject::tr("Unsupported sync content.");
		return {};
	}
	const QByteArray icon = QByteArray::fromBase64(payload.value("icon").toString().toLatin1());
	if (icon.size() < 128 * 1024)
		value.icon =
			SourceIcon::bounded(readImage(icon, SourceIcon::maxPixels * SourceIcon::maxPixels));
	return value;
}
QString digest(const QByteArray &value)
{
	return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
QString key(const Snapshot &value)
{
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(value.kind.toUtf8());
	hash.addData(QByteArray(1, '\0'));
	if (value.kind == "text")
		hash.addData(value.text.trimmed().toUtf8());
	else if (value.kind == "links")
		hash.addData(
			QJsonDocument(QJsonArray::fromStringList(value.urls)).toJson(QJsonDocument::Compact));
	else {
		const QImage image = value.image.convertToFormat(QImage::Format_RGBA8888);
		hash.addData(
			QByteArray::number(image.width()) + "x" + QByteArray::number(image.height()) + ":");
		for (int row = 0; row < image.height(); ++row)
			hash.addData(QByteArrayView(
				reinterpret_cast<const char *>(image.constScanLine(row)), image.width() * 4));
	}
	return QString::fromLatin1(hash.result().toHex());
}
QByteArray bytes(const QJsonObject &document)
{
	return QJsonDocument(document).toJson(QJsonDocument::Compact);
}
void writeFavoriteDetails(QJsonObject &document, const FavoriteDetails &details)
{
	if (!details.nameModified && !details.positionModified) return;
	document["version"] = 3;
	document["favoriteName"] = details.name;
	document["favoriteNameModified"] = details.nameModified;
	document["favoritePosition"] = details.position;
	document["favoritePositionModified"] = details.positionModified;
}

QByteArray document(Snapshot value, bool deleted, const QString &knownKey, QString *error)
{
	QJsonObject payload;
	QString contentKey = knownKey;
	if (!deleted || contentKey.isEmpty()) {
		payload = encode(value, error);
		if (!error->isEmpty() || payload.isEmpty()) return {};
		const auto normalized = decode(payload, error);
		if (!error->isEmpty()) return {};
		contentKey = key(normalized);
	}
	if (!value.events.copy.valid() && !deleted)
		value.events.copy = EventVersion::legacy(value.time.toMSecsSinceEpoch());
	if (value.favorite && !value.favoriteModified)
		value.favoriteModified = qMax(qint64(1), value.time.toMSecsSinceEpoch());
	if (!value.events.favorite.valid())
		value.events.favorite = EventVersion::legacy(value.favoriteModified);
	if (!value.events.name.valid())
		value.events.name = EventVersion::legacy(value.favoriteDetails.nameModified);
	if (!value.events.position.valid())
		value.events.position = EventVersion::legacy(value.favoriteDetails.positionModified);

	QJsonObject o{{"version", 4}, {"key", contentKey},
		{"modified",
			qMax(qMax(qint64(1), value.events.latest().physical),
				qMax(value.favoriteModified,
					qMax(value.favoriteDetails.nameModified,
						value.favoriteDetails.positionModified)))},
		{"deleted", deleted}, {"events", value.events.json()},
		{"favoriteModified", value.favoriteModified}};
	o["favorite"] = value.favorite;
	o["favoriteName"] = value.favoriteDetails.name;
	o["favoriteNameModified"] = value.favoriteDetails.nameModified;
	o["favoritePosition"] = value.favoriteDetails.position;
	o["favoritePositionModified"] = value.favoriteDetails.positionModified;
	if (!deleted) {
		o["copied"] = value.time.toMSecsSinceEpoch();
		o["content"] = payload;
	}
	return bytes(o);
}

Snapshot merge(Snapshot target, const Snapshot &input)
{
	auto source = input;
	auto normalize = [](Snapshot &value) {
		if (value.favorite && !value.favoriteModified)
			value.favoriteModified = qMax(qint64(1), value.time.toMSecsSinceEpoch());
		if (!value.events.copy.valid())
			value.events.copy = EventVersion::legacy(value.time.toMSecsSinceEpoch());
		if (!value.events.favorite.valid())
			value.events.favorite = EventVersion::legacy(value.favoriteModified);
		if (!value.events.name.valid())
			value.events.name = EventVersion::legacy(value.favoriteDetails.nameModified);
		if (!value.events.position.valid())
			value.events.position = EventVersion::legacy(value.favoriteDetails.positionModified);
	};
	normalize(target);
	normalize(source);
	const auto previous = target;
	if (source.events.copy > target.events.copy ||
		(!target.events.copy.valid() && source.time > target.time)) {
		target = source;
	}
	const bool chooseSource = source.events.source > previous.events.source ||
		(source.events.source == previous.events.source &&
			(source.events.sourceRevision > previous.events.sourceRevision ||
				(source.events.sourceRevision == previous.events.sourceRevision &&
					SourceIcon::key(source.icon) > SourceIcon::key(previous.icon))));
	const auto &icon = chooseSource ? source : previous;
	const auto &fallback = chooseSource ? previous : source;
	target.icon = icon.icon.isNull() ? fallback.icon : icon.icon;
	target.events.source = icon.icon.isNull() ? fallback.events.source : icon.events.source;
	target.events.sourceCaptured =
		icon.icon.isNull() ? fallback.events.sourceCaptured : icon.events.sourceCaptured;
	const bool favorite = source.events.favorite > previous.events.favorite ||
		(source.events.favorite == previous.events.favorite &&
			(source.favoriteModified > previous.favoriteModified ||
				(source.favoriteModified == previous.favoriteModified && !source.favorite &&
					previous.favorite)));
	target.favorite = favorite ? source.favorite : previous.favorite;
	target.favoriteModified = favorite ? source.favoriteModified : previous.favoriteModified;
	const bool name = source.events.name > previous.events.name ||
		(source.events.name == previous.events.name &&
			source.favoriteDetails.name > previous.favoriteDetails.name);
	const bool position = source.events.position > previous.events.position ||
		(source.events.position == previous.events.position &&
			source.favoriteDetails.position < previous.favoriteDetails.position);
	target.favoriteDetails.name =
		name ? source.favoriteDetails.name : previous.favoriteDetails.name;
	target.favoriteDetails.nameModified =
		name ? source.favoriteDetails.nameModified : previous.favoriteDetails.nameModified;
	target.favoriteDetails.position =
		position ? source.favoriteDetails.position : previous.favoriteDetails.position;
	target.favoriteDetails.positionModified = position ? source.favoriteDetails.positionModified
													   : previous.favoriteDetails.positionModified;
	target.events.observe(previous.events);
	target.events.observe(source.events);
	target.events.source = icon.icon.isNull() ? fallback.events.source : icon.events.source;
	target.events.sourceCaptured =
		icon.icon.isNull() ? fallback.events.sourceCaptured : icon.events.sourceCaptured;
	target.events.sourceRevision =
		icon.icon.isNull() ? fallback.events.sourceRevision : icon.events.sourceRevision;
	return target;
}

static Record parseRecord(const QByteArray &data, QString *error, bool verifyContent)
{
	Record record;
	if (data.size() > maxDocumentBytes) {
		*error = QObject::tr("Sync data exceeds the size limit.");
		return {};
	}
	QJsonParseError parseError;
	const auto json = QJsonDocument::fromJson(data, &parseError);
	const auto object = json.object();
	record.key = object.value("key").toString();
	record.modified = object.value("modified").toInteger();
	record.copied = object.value("copied").toInteger();
	record.deleted = object.value("deleted").toBool();
	record.document = object;
	record.id = digest(data);
	record.favorite = object.value("favorite").toBool();
	record.favoriteModified = object.value("favoriteModified").toInteger();
	const auto keyBytes = record.key.toLatin1();
	const int version = object.value("version").toInt();
	if (version == 4 || (version == 3 && !record.deleted)) {
		record.favoriteDetails = {object.value("favoriteName").toString(),
			object.value("favoriteNameModified").toInteger(-1),
			object.value("favoritePosition").toInteger(-1),
			object.value("favoritePositionModified").toInteger(-1)};
	}
	const auto &details = record.favoriteDetails;
	const bool invalidDetails = (version == 4 || (version == 3 && !record.deleted)) &&
		(!object.value("favoriteName").isString() ||
			details.name.size() > FavoriteDetails::maxNameLength ||
			details.name != details.name.simplified() || details.nameModified < 0 ||
			details.nameModified > record.modified ||
			(!details.nameModified && !details.name.isEmpty()) || details.position < 0 ||
			details.position > FavoriteDetails::maxPosition || details.positionModified < 0 ||
			details.positionModified > record.modified ||
			(!details.positionModified && details.position != 0));
	if (parseError.error != QJsonParseError::NoError ||
		(version != 1 && version != 2 && version != 3 && version != 4) || invalidDetails ||
		record.key.size() != 64 || QByteArray::fromHex(keyBytes).toHex() != keyBytes ||
		record.modified <= 0 ||
		(version != 4 && record.modified > QDateTime::currentMSecsSinceEpoch() + 5 * 60 * 1000) ||
		(object.contains("favorite") && !object.value("favorite").isBool()) ||
		record.favoriteModified < 0 || record.favoriteModified > record.modified ||
		(record.favorite && ((version != 4 && record.deleted) || !record.favoriteModified))) {
		*error = QObject::tr("Invalid sync record or incompatible version.");
		return {};
	}
	if (!record.deleted) {
		const qint64 copied = object.value("copied").toInteger();
		Snapshot content;
		if (verifyContent)
			content = decode(object.value("content").toObject(), error);
		else {
			const auto icon = QByteArray::fromBase64(
				object.value("content").toObject().value("icon").toString().toLatin1());
			content.icon =
				SourceIcon::bounded(readImage(icon, SourceIcon::maxPixels * SourceIcon::maxPixels));
		}
		if (!error->isEmpty()) return {};
		record.hasIcon = !content.icon.isNull();
		record.iconCaptured = SourceIcon::captured(content.icon);
		if (record.hasIcon) record.iconKey = SourceIcon::key(content.icon);
		if (copied <= 0 || copied > EventVersion::maxPhysical ||
			(version != 4 && copied > record.modified) ||
			(verifyContent && key(content) != record.key)) {
			*error = QObject::tr("The sync record failed its content check.");
			return {};
		}
	}
	if (version == 4) {
		record.events = EventState::parse(object.value("events").toObject());
		for (const auto &v : {record.events.copy, record.events.erase, record.events.source,
				 record.events.favorite, record.events.name, record.events.position}) {
			if (v.physical < 0 || v.physical > EventVersion::maxPhysical || v.logical < 0 ||
				v.logical > EventVersion::maxLogical || v.device.size() > 80) {
				*error = QObject::tr("Invalid sync event version.");
				return {};
			}
		}
		if (record.events.sourceRevision < 0 || record.events.sourceRevision > 1 ||
			record.events.legacyPresence < 0 ||
			record.events.legacyPresence > EventVersion::maxPhysical ||
			record.events.erasedAt < 0 || record.events.sourceCaptured < 0 ||
			record.events.deleted() != record.deleted ||
			(!record.deleted && !record.events.copy.valid()) ||
			(record.events.source.valid() && !record.deleted && !record.hasIcon)) {
			*error = QObject::tr("Invalid sync event state.");
			return {};
		}
	} else {
		record.events.copy = EventVersion::legacy(record.copied);
		// Old undo and metadata records retain copied. Preserve their live
		// ordering without advancing the copy time or claiming a source observation.
		if (!record.deleted) record.events.legacyPresence = record.modified;
		if (record.deleted) {
			record.events.erase = EventVersion::legacy(record.modified);
			record.events.erasedAt = record.modified;
		}
		record.events.favorite = EventVersion::legacy(record.favoriteModified);
		record.events.name = EventVersion::legacy(record.favoriteDetails.nameModified);
		record.events.position = EventVersion::legacy(record.favoriteDetails.positionModified);
		if (record.deleted)
			record.events.favorite = record.events.name = record.events.position =
				record.events.erase;
		// Legacy PNG timestamps are hints, never proof of a new observation.
	}
	return record;
}
Record parse(const QByteArray &data, QString *error)
{
	return parseRecord(data, error, true);
}
Record inspect(const QByteArray &data, QString *error)
{
	return parseRecord(data, error, false);
}

QByteArray reduce(const QList<Record> &records,
	const std::function<QByteArray(const QString &)> &read, QString *error)
{
	if (records.isEmpty()) return {};
	const auto contentKey = records.first().key;
	for (const auto &record : records)
		if (record.key != contentKey) {
			*error = QObject::tr("Invalid sync event state.");
			return {};
		}
	auto object = [&read](const Record &record) {
		return record.document.isEmpty() ? QJsonDocument::fromJson(read(record.id)).object()
										 : record.document;
	};
	EventState state;
	Record copy, source, favorite, name, position;
	qint64 favoriteModified = 0;
	for (const auto &record : records) {

		state.observe(record.events);
		favoriteModified = qMax(favoriteModified, record.favoriteModified);
		if (!record.deleted &&
			(copy.id.isEmpty() || record.events.copy > copy.events.copy ||
				(record.events.copy == copy.events.copy && record.id > copy.id)))
			copy = record;
		if (!record.deleted && record.hasIcon &&
			(source.id.isEmpty() || record.events.source > source.events.source ||
				(record.events.source == source.events.source &&
					(record.events.sourceRevision > source.events.sourceRevision ||
						(record.events.sourceRevision == source.events.sourceRevision &&
							record.iconKey > source.iconKey)))))
			source = record;
		if (favorite.id.isEmpty() || record.events.favorite > favorite.events.favorite ||
			(record.events.favorite == favorite.events.favorite && !record.favorite))
			favorite = record;
		if (name.id.isEmpty() || record.events.name > name.events.name ||
			(record.events.name == name.events.name &&
				record.favoriteDetails.name > name.favoriteDetails.name))
			name = record;
		if (position.id.isEmpty() || record.events.position > position.events.position ||
			(record.events.position == position.events.position &&
				record.favoriteDetails.position < position.favoriteDetails.position))
			position = record;
	}
	const bool deleted = state.deleted();
	if (!deleted && copy.id.isEmpty()) {
		*error = QObject::tr("Invalid sync event state.");
		return {};
	}
	QJsonObject payload;
	if (!deleted) {
		payload = object(copy).value("content").toObject();
		if (!source.id.isEmpty())
			payload["icon"] = object(source).value("content").toObject().value("icon");
		else
			payload.remove("icon");
		state.source = source.events.source;
		state.sourceCaptured = source.events.sourceCaptured;
		state.sourceRevision = source.events.sourceRevision;
	}
	QJsonObject document{{"version", 4}, {"key", contentKey},
		{"modified",
			qMax(qMax(qint64(1), state.latest().physical),
				qMax(favoriteModified,
					qMax(name.favoriteDetails.nameModified,
						position.favoriteDetails.positionModified)))},
		{"deleted", deleted}, {"events", state.json()}, {"favoriteModified", favoriteModified}};
	document["favorite"] = favorite.favorite;
	document["favoriteName"] = name.favoriteDetails.name;
	document["favoriteNameModified"] = name.favoriteDetails.nameModified;
	document["favoritePosition"] = position.favoriteDetails.position;
	document["favoritePositionModified"] = position.favoriteDetails.positionModified;
	if (!deleted) {
		// A compacted tombstone can retain a later legacy copy version after
		// dropping its payload. Its physical component is the original copy
		// time; a restored older payload must not rewind that time.
		document["copied"] = state.copy.logical == 0 && state.copy.device.isEmpty()
			? state.copy.physical : copy.copied;
		document["content"] = payload;
	}
	return bytes(document);
}
QByteArray expire(QByteArray document, qint64 now, QString *error)
{
	const auto record = inspect(document, error);
	if (!error->isEmpty() || record.deleted || record.favorite || !record.favoriteModified ||
		!expired(record, now))
		return document;
	auto state = record.events;
	state.erase = {qMax(state.latest().physical + 1,
					   record.copied + qint64(HistoryPolicy::retentionDays) * 86400000),
		0, "retention"};
	state.erasedAt = qMax(
		record.favoriteModified, record.copied + qint64(HistoryPolicy::retentionDays) * 86400000);
	auto tomb = record.document;
	tomb["events"] = state.json();
	tomb["modified"] = state.latest().physical;
	tomb["deleted"] = true;
	tomb.remove("content");
	tomb.remove("copied");
	return bytes(tomb);
}

bool expired(const Record &record, qint64 now)
{
	/* Keep favorite removal markers so an offline device cannot revive an
	 * old favorite after ordinary deletion markers have aged out. */
	if (record.deleted ? record.favoriteModified > 0 : record.favorite) return false;
	const qint64 age = now -
		(record.deleted ? (record.events.erasedAt ? record.events.erasedAt : record.modified)
						: record.copied);
	return age >= qint64(record.deleted ? 60 : HistoryPolicy::retentionDays) * 86400000;
}
} // namespace SyncContent
