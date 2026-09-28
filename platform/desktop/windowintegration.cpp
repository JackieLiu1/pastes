#include "platform/windowintegration.h"

#include <QApplication>
#include <QCursor>
#include <QScreen>
#include <QWidget>

namespace Platform {

const PanelAppearance &panelAppearance(void)
{
	static const PanelAppearance appearance;
	return appearance;
}

const DialogAppearance &dialogAppearance(void)
{
	static const DialogAppearance appearance;
	return appearance;
}

QRect panelGeometry(QScreen *screen)
{
	if (!screen) screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen) screen = QApplication::primaryScreen();
	if (!screen) return QRect();
	const QRect area = screen->availableGeometry();
	const int height = qMin(area.height(), qBound(300, area.height()*38/100, 450));
	return QRect(area.x(), area.bottom()-height+1, area.width(), height);
}

QSize cardSize(const QSize &panelSize)
{
	return QSize(qBound(210, panelSize.width()/6, 280), qMax(110, panelSize.height()-136));
}

void initializePanel(QWidget *widget)
{
	widget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
		Qt::BypassWindowManagerHint | Qt::SplashScreen);
}

void preparePanel(QWidget *) {}
void updatePanelBackdrop(QWidget *) {}
void watchPanelDismissal(QWidget *, const std::function<void(bool)> &) {}
void initializeDialog(QWidget *) {}
void prepareDialog(QWidget *) {}
void initializePreview(QWidget *) {}
void preparePreview(QWidget *) {}

void activatePanel(QWidget *widget)
{
	widget->raise();
	widget->activateWindow();
}

void activatePreview(QWidget *widget)
{
	activatePanel(widget);
}

}
