#include "platform/windowintegration.h"

#include <QWidget>

bool Platform::isOwnedWindow(QWidget *owner, QWidget *window)
{
	for (QWidget *parent = window; parent; parent = parent->parentWidget())
		if (parent == owner)
			return true;
	return false;
}

void Platform::initializeDragOverlay(QWidget *widget)
{
	if (QWidget *owner = widget->parentWidget()) {
		/* A managed tool window can appear in the taskbar and sit below an
		 * unmanaged X11 panel. Inherit its stacking policy before mapping. */
		widget->setWindowFlags(widget->windowFlags() | (owner->window()->windowFlags() &
			(Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint)));
	}
}
