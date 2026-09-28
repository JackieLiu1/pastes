#include <QEvent>
#include <QKeyEvent>

#include "mainframe.h"

MainFrame::MainFrame(QWidget *parent) : RoundedWidget(RoundedRole::Panel, parent)
{
	this->setObjectName("MainFrame");
	this->setAttribute(Qt::WA_StyledBackground);
}

bool MainFrame::event(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *ke = static_cast<QKeyEvent *>(event);

		switch (ke->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
			if (ke->modifiers() & Qt::ShiftModifier)
				emit this->selectPlainTextItem();
			else
				emit this->selectItem();
			return true;
		case Qt::Key_Escape:
			emit this->hideWindow();
			return true;
		case Qt::Key_Tab:
			emit this->moveFocusPrevNext(ke->modifiers().testFlag(Qt::ShiftModifier), true);
			return true;
		case Qt::Key_Backtab:
			emit this->moveFocusPrevNext(true, true);
			return true;
		case Qt::Key_Left:
			emit this->moveFocusPrevNext(true, false);
			return true;
		case Qt::Key_Right:
			emit this->moveFocusPrevNext(false, false);
			return true;
		}
	}

	return QWidget::event(event);
}
