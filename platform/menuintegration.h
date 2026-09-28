#ifndef PLATFORM_MENUINTEGRATION_H
#define PLATFORM_MENUINTEGRATION_H

class QAction;
class QMenu;
class QWidget;

namespace Platform {

struct MenuAppearance
{
	bool showPanelAction = true;
	bool standardShortcuts = false;
	bool preferencesSeparator = false;
};

const MenuAppearance &menuAppearance(void);
QAction *createSettingsAction(QWidget *owner);
QAction *createQuitAction(QWidget *owner);
void popupMenu(QMenu *menu, QWidget *anchor);

}
#endif
