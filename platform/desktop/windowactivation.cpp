#include "platform/windowintegration.h"

#include <QWidget>

void Platform::watchPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss)
{
	watchQtPanelDismissal(widget, dismiss);
}

void Platform::activatePanel(QWidget *widget)
{
	widget->raise();
	widget->activateWindow();
}

void Platform::activatePreview(QWidget *widget)
{
	activatePanel(widget);
}
