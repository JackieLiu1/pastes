#ifndef WINDOW_MAC_H
#define WINDOW_MAC_H

#include <functional>

class QWidget;
class QScreen;

void configureMacApplication(void);
QScreen *macPanelScreen(void);
/* Dismiss with animation on focus loss, immediately after a Space change. */
void watchMacPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss);
void prepareMacPanel(QWidget *widget);
void updateMacPanelBackdrop(QWidget *widget);
void activateMacPanel(QWidget *widget);

#endif // WINDOW_MAC_H
