#include "platform/windowintegration.h"

#include <QWidget>

bool Platform::isOwnedWindow(QWidget *owner, QWidget *window)
{
	for (QWidget *parent = window; parent; parent = parent->parentWidget())
		if (parent == owner)
			return true;
	return false;
}
