#ifndef PASTES_TEST_COLORCONTRAST_H
#define PASTES_TEST_COLORCONTRAST_H

#include <QColor>
#include <cmath>

inline qreal relativeLuminance(const QColor &color)
{
	const auto linear = [](qreal channel) {
		return channel <= 0.04045 ? channel/12.92 : std::pow((channel+0.055)/1.055, 2.4);
	};
	return 0.2126*linear(color.redF())+0.7152*linear(color.greenF())+0.0722*linear(color.blueF());
}

inline qreal contrastRatio(const QColor &foreground, const QColor &background)
{
	const qreal first = relativeLuminance(foreground), second = relativeLuminance(background);
	return (qMax(first, second)+0.05)/(qMin(first, second)+0.05);
}
#endif
