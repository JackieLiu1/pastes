#ifndef SOURCEICON_H
#define SOURCEICON_H

#include <QImage>
#include <QCryptographicHash>

namespace SourceIcon {

/* Keep real pixels for HiDPI consumers, without enlarging small sources. */
constexpr int maxPixels = 128;
constexpr char captureTimeKey[] = "pastes.sourceIcon.captured";

inline qint64 captured(const QImage &image)
{
	if (image.isNull()) return 0;
	bool valid = false;
	const qint64 time = image.text(QLatin1String(captureTimeKey)).toLongLong(&valid);
	return valid && time > 0 ? time : 0;
}

/* Retain old PNG hints for display and migration. EventState is the sole
 * authority for trusted source observations and their ordering. */
inline QImage observed(QImage image, qint64 time)
{
	if (!image.isNull()) image.setText(QLatin1String(captureTimeKey), QString::number(time));
	return image;
}

inline QImage bounded(const QImage &image)
{
	if (image.width() <= maxPixels && image.height() <= maxPixels)
		return image;
	QImage result =
		image.scaled(maxPixels, maxPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	if (captured(image)) result = observed(result, captured(image));
	return result;
}

inline QByteArray key(const QImage &image)
{
	const QImage pixels = bounded(image).convertToFormat(QImage::Format_RGBA8888);
	QCryptographicHash hash(QCryptographicHash::Sha256);
	hash.addData(
		QByteArray::number(pixels.width()) + "x" + QByteArray::number(pixels.height()) + ":");
	for (int y = 0; y < pixels.height(); ++y)
		hash.addData(QByteArrayView(
			reinterpret_cast<const char *>(pixels.constScanLine(y)), pixels.width() * 4));
	/* Prefer higher resolution for equal observations, then break ties by
	 * pixels so devices converge regardless of record delivery order. */
	return QByteArray::number(qint64(pixels.width()) * pixels.height()).rightJustified(8, '0') +
		hash.result().toHex();
}
}

#endif
