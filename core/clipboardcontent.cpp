#include "core/clipboardcontent.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QImage>
#include <QMimeData>
#include <QMimeDatabase>
#include <QUrl>

bool ClipboardContent::prefersImage(const QMimeData &mime)
{
	if (!mime.hasImage() || (mime.hasHtml() && !mime.text().trimmed().isEmpty()))
		return false;
	const QList<QUrl> urls = mime.urls();
	/* A bitmap cannot represent a collection of files or a web link. */
	if (urls.isEmpty()) return true;
	if (urls.size() != 1 || !urls.first().isLocalFile()) return false;
	/* Stored file copies can contain a synthesized file icon. Treat only
	 * accompanying raster-image paths as bitmap content, without file I/O. */
	const QMimeType type = QMimeDatabase().mimeTypeForFile(urls.first().fileName(), QMimeDatabase::MatchExtension);
	/* Compressed SVG can inherit only application/gzip in the system database. */
	return type.name().startsWith("image/") && !type.inherits("image/svg+xml") &&
		!type.inherits("image/svg+xml-compressed");
}

QByteArray ClipboardContent::fingerprint(const QMimeData &mime)
{
	QCryptographicHash hash(QCryptographicHash::Md5);
	if (mime.hasUrls() && !mime.urls().isEmpty()) {
		for (const QUrl &url : mime.urls()) hash.addData(url.toEncoded());
		return hash.result();
	}
	if (mime.hasHtml() && !mime.text().trimmed().isEmpty()) {
		hash.addData(mime.text().trimmed().toLocal8Bit());
		return hash.result();
	}
	if (mime.hasImage()) {
		/* Decode a compressed original only once for this identity check. */
		const QImage image = qvariant_cast<QImage>(mime.imageData());
		if (!image.isNull()) return imageFingerprint(image);
	}
	if (mime.hasText() && !mime.text().trimmed().isEmpty()) {
		hash.addData(mime.text().trimmed().toLocal8Bit());
		return hash.result();
	}
	return QByteArray();
}

QByteArray ClipboardContent::imageFingerprint(const QImage &image)
{
	if (image.isNull()) return {};
	QCryptographicHash hash(QCryptographicHash::Md5);
	for (int row = 0; row < image.height(); ++row)
		hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(row)), image.bytesPerLine()));
	return hash.result();
}

QByteArray ClipboardContent::imageContentKey(const QImage &image)
{
	if (image.isNull()) return {};
	/* Keep HDR/float samples intact. Regular clipboard bitmaps share an
	 * 8-bit RGBA representation without row padding. */
	const QImage pixels = image.depth() > 32 ? image :
		image.convertToFormat(QImage::Format_RGBA8888);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(QByteArray::number(pixels.width())+"x"+
		QByteArray::number(pixels.height())+":"+QByteArray::number(pixels.format())+":");
	hash.addData(pixels.colorSpace().iccProfile());
	const qsizetype rowBytes = qsizetype(pixels.width())*(pixels.depth()/8);
	for (int row = 0; row < pixels.height(); ++row)
		hash.addData(QByteArrayView(reinterpret_cast<const char *>(pixels.constScanLine(row)), rowBytes));
	return hash.result();
}

bool ClipboardContent::sameImageColors(const QImage &left, const QImage &right)
{
	auto ordinaryRgb = [](const QImage &image) {
		switch (image.format()) {
		case QImage::Format_RGB32:
		case QImage::Format_ARGB32:
		case QImage::Format_ARGB32_Premultiplied:
		case QImage::Format_RGB888:
		case QImage::Format_RGBX8888:
		case QImage::Format_RGBA8888:
		case QImage::Format_RGBA8888_Premultiplied:
			return true;
		default:
			return false;
		}
	};
	if (left.isNull() || left.size() != right.size() ||
		!ordinaryRgb(left) || !ordinaryRgb(right) ||
		!left.colorSpace().isValid() || !right.colorSpace().isValid()) return false;
	const bool converted = left.colorSpace() != right.colorSpace();
	QImage a = left.convertToFormat(QImage::Format_RGBA8888);
	QImage b = right.convertToFormat(QImage::Format_RGBA8888);
	if (a.isNull() || b.isNull()) return false;
	if (converted) {
		a.convertToColorSpace(QColorSpace::SRgb);
		b.convertToColorSpace(QColorSpace::SRgb);
		if (a.colorSpace() != QColorSpace(QColorSpace::SRgb) ||
			b.colorSpace() != QColorSpace(QColorSpace::SRgb)) return false;
	}
	const int tolerance = converted ? 1 : 0;
	for (int row = 0; row < a.height(); ++row) {
		const auto *first = a.constScanLine(row);
		const auto *second = b.constScanLine(row);
		for (int column = 0; column < a.width(); ++column) {
			const qsizetype offset = qsizetype(column)*4;
			if (first[offset+3] != second[offset+3]) return false;
			for (int channel = 0; channel < 3; ++channel)
				if (qAbs(int(first[offset+channel])-int(second[offset+channel])) > tolerance)
					return false;
		}
	}
	return true;
}
