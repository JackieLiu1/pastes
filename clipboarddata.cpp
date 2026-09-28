#include "clipboarddata.h"

#include <QBuffer>
#include <QImageReader>

namespace {

const QString imageType = QStringLiteral("application/x-qt-image");

class StoredImageMimeData final : public QMimeData
{
public:
	StoredImageMimeData(const QByteArray &encoded, int format, qreal ratio) :
		m_encoded(encoded), m_format(format), m_ratio(ratio)
	{
		setData(imageType, QByteArray());
	}

	const QByteArray &encoded(void) const { return m_encoded; }
	int imageFormat(void) const { return m_format; }
	qreal imageRatio(void) const { return m_ratio; }

protected:
	QVariant retrieveData(const QString &type, QMetaType preferred) const override
	{
		if (type != imageType || preferred == QMetaType::fromType<QByteArray>())
			return QMimeData::retrieveData(type, preferred);
		QImage image = QImage::fromData(m_encoded);
		if (!image.isNull() && m_format != QImage::Format_Invalid)
			image = image.convertToFormat(QImage::Format(m_format));
		image.setDevicePixelRatio(m_ratio);
		return image.isNull() ? QVariant() : QVariant::fromValue(image);
	}

private:
	QByteArray m_encoded;
	int m_format;
	qreal m_ratio;
};

void copyFormats(const QMimeData *source, QMimeData *target)
{
	for (const QString &format : source->formats())
		target->setData(format, source->data(format));
}

}

QMimeData *ClipboardData::duplicate(const QMimeData *source)
{
	if (const auto *stored = dynamic_cast<const StoredImageMimeData *>(source))
		return withStoredImage(source, stored->encoded(), stored->imageFormat(), stored->imageRatio());
	auto *mime = new QMimeData;
	copyFormats(source, mime);
	if (source->hasImage())
		mime->setImageData(source->imageData());
	return mime;
}

QMimeData *ClipboardData::withStoredImage(const QMimeData *source, const QByteArray &encoded, int format, qreal ratio)
{
	auto *mime = new StoredImageMimeData(encoded, format, ratio);
	mime->clear();
	copyFormats(source, mime);
	if (!mime->hasFormat(imageType)) mime->setData(imageType, QByteArray());
	return mime;
}

QByteArray ClipboardData::storedImage(const QMimeData *source)
{
	const auto *stored = dynamic_cast<const StoredImageMimeData *>(source);
	return stored ? stored->encoded() : QByteArray();
}

QImage ClipboardData::previewImage(const QMimeData *source, QSize *originalSize)
{
	if (const auto *stored = dynamic_cast<const StoredImageMimeData *>(source)) {
		QBuffer buffer;
		buffer.setData(stored->encoded());
		buffer.open(QIODevice::ReadOnly);
		QImageReader reader(&buffer);
		const QSize size = reader.size();
		if (originalSize) *originalSize = size;
		if (!size.isValid()) return QImage();
		if (size.width() > previewPixels || size.height() > previewPixels)
			reader.setScaledSize(size.scaled(previewPixels, previewPixels, Qt::KeepAspectRatio));
		return reader.read();
	}
	const QImage image = qvariant_cast<QImage>(source->imageData());
	if (originalSize) *originalSize = image.size();
	return image.width() > previewPixels || image.height() > previewPixels ?
		image.scaled(previewPixels, previewPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
}

bool ClipboardData::canCompressImage(const QImage &image)
{
	const int limit = QImageReader::allocationLimit();
	/* PNG decoding can expand RGB input to four bytes per pixel. Keep
	 * oversized images live if a later decode would exceed Qt's limit. */
	if (image.isNull() || (limit > 0 && quint64(image.width())*image.height()*4 > quint64(limit)*1024*1024))
		return false;
	/* PNG preserves these RGB formats. Keep grayscale and uncommon/HDR
	 * formats in memory: PNG can discard incompatible color profiles. */
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
}
