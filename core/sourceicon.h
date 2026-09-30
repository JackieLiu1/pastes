#ifndef SOURCEICON_H
#define SOURCEICON_H

#include <QImage>

namespace SourceIcon {

/* Keep real pixels for HiDPI consumers, without enlarging small sources. */
constexpr int maxPixels = 128;

inline QImage bounded(const QImage &image)
{
	if (image.width() <= maxPixels && image.height() <= maxPixels)
		return image;
	return image.scaled(maxPixels, maxPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

}

#endif
