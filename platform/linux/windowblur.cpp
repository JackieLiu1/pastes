#include "platform/windowintegration.h"

#include <QWidget>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <KF5/KWindowSystem/KWindowEffects>
#endif

const Platform::PanelAppearance &Platform::panelAppearance(void)
{
	static const PanelAppearance appearance;
	return appearance;
}

void Platform::preparePanel(QWidget *) {}
void Platform::updatePanelBackdrop(QWidget *) {}

void Platform::enablePanelBlur(QWidget *widget)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	KWindowEffects::enableBlurBehind(widget->winId(), true);
#else
	Q_UNUSED(widget);
#endif
}
