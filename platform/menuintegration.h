#ifndef PLATFORM_MENUINTEGRATION_H
#define PLATFORM_MENUINTEGRATION_H

class QAction;
class QMenu;
class QPoint;
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
/* GUI thread; menu and owner must outlive tracking. Return the chosen
 * action at a global position, or nullptr on cancellation. */
QAction *execMenuAt(QMenu *menu, QWidget *owner, const QPoint &position);

}
#endif
