#include "core/clipboardcontent.h"

#include <QByteArrayView>
#include <QCryptographicHash>
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
	return type.name().startsWith("image/") && !type.inherits("image/svg+xml");
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
