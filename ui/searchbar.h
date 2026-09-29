#ifndef SEARCHBAR_H
#define SEARCHBAR_H

#include <QLabel>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QAction>
#include "ui/roundedwidgets.h"

class LineEdit : public QLineEdit
{
	Q_OBJECT
public:
	LineEdit(QWidget *parent = nullptr, int parent_width = 0, int parent_height = 0);
	void updateIcon(void);

protected:
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	void inputMethodEvent(QInputMethodEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void changeEvent(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	bool event(QEvent * event) override;

Q_SIGNALS:
	void focusIn(void);
	void focusOut(void);
	void hideWindow(void);
	void selectItem(void);
	void selectPlainTextItem(void);
	void moveFocusPrevNext(bool prev, bool wrap);

private:
	QAction			*m_searchAction;
	RoundedSurface		m_surface;
	bool			m_composing = false;
};

class SearchBar : public QWidget
{
	Q_OBJECT
public:
	SearchBar(QWidget *parent = nullptr, int width = 0, int height = 0);

private:
	LineEdit		*m_search_edit;

Q_SIGNALS:
	void moveFocusPrevNext(bool prev, bool wrap);
	void selectItem(void);
	void selectPlainTextItem(void);
	void hideWindow(void);
	void textChanged(const QString &);
};

#endif // SEARCHBAR_H
