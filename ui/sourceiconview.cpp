#include "ui/sourceiconview.h"

#include <QPainter>
#include <QtMath>

namespace {
QRect visibleBounds(const QImage &image, int minimumAlpha)
{
	int left = image.width(), top = image.height(), right = -1, bottom = -1;
	for (int y = 0; y < image.height(); ++y) {
		const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
		for (int x = 0; x < image.width(); ++x) {
			if (qAlpha(line[x]) < minimumAlpha)
				continue;
			left = qMin(left, x); right = qMax(right, x);
			top = qMin(top, y); bottom = qMax(bottom, y);
		}
	}
	return right < left ? QRect() : QRect(QPoint(left, top), QPoint(right, bottom));
}
}

QPixmap SourceIconView::pixmap(const QImage &image, int logicalSize, qreal devicePixelRatio)
{
	if (image.isNull() || logicalSize <= 0 || devicePixelRatio <= 0)
		return {};
	QImage source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
	source.setDevicePixelRatio(1);
	/* Native app icons have different padding and faint drop shadows. Fit
	 * the visible artwork, leaving the same breathing room for every OS. */
	QRect bounds = visibleBounds(source, 128);
	if (bounds.isEmpty()) bounds = visibleBounds(source, 1);
	if (bounds.isEmpty()) return {};
	/* Equal source margins also make edge sampling independent of the
	 * original canvas. QImage::copy fills any out-of-bounds area with zero. */
	const int margin = qCeil(qMax(bounds.width(), bounds.height())/18.0);
	source = source.copy(bounds.adjusted(-margin, -margin, margin, margin));
	const int pixels = qMax(1, qRound(logicalSize*devicePixelRatio));
	const int artworkPixels = qMax(1, qRound(pixels*0.9));
	const qreal scale = qreal(artworkPixels)/qMax(bounds.width(), bounds.height());
	/* QPainter's smooth transform samples neighboring pixels but can alias
	 * details when a large native icon shrinks to a small header. Use the
	 * image scaler's area filter, then place the result on physical pixels. */
	source = source.scaled(QSize(qMax(1, qRound(source.width()*scale)), qMax(1, qRound(source.height()*scale))),
		Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	QImage rendered(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
	rendered.fill(Qt::transparent);
	QPainter painter(&rendered);
	/* Draw the whole image so antialiasing and shadows can use the margin. */
	painter.drawImage(QPoint((pixels-source.width())/2, (pixels-source.height())/2), source);
	painter.end();
	rendered.setDevicePixelRatio(devicePixelRatio);
	return QPixmap::fromImage(rendered);
}
