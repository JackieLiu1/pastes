#ifndef WINDOW_MAC_H
#define WINDOW_MAC_H

class QWidget;
class QScreen;

void configureMacApplication(void);
QScreen *macPanelScreen(void);
void prepareMacPanel(QWidget *widget);
void updateMacPanelBackdrop(QWidget *widget);
void activateMacPanel(QWidget *widget);

#endif // WINDOW_MAC_H
