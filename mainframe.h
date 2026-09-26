#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "roundedwidgets.h"

class MainFrame : public RoundedWidget
{
	Q_OBJECT

public:
	MainFrame(QWidget *parent = nullptr);

protected:
	bool event(QEvent *event);

signals:
	void moveFocusPrevNext(bool);
	void selectItem(void);
	void selectPlainTextItem(void);
	void hideWindow(void);
};

#endif // MAINFRAME_H
