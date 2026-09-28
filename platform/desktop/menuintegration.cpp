#include "platform/menuintegration.h"

#include <QMenu>
#include <QWidget>

const Platform::MenuAppearance &Platform::menuAppearance(void)
{
	static const MenuAppearance appearance;
	return appearance;
}

void Platform::popupMenu(QMenu *menu, QWidget *anchor)
{
	menu->exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}
