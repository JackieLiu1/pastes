#ifndef SEARCHBAR_H
#define SEARCHBAR_H

#include <QLabel>
#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QAction>
#include <QPropertyAnimation>

class LineEdit : public QLineEdit
{
	Q_OBJECT
public:
	LineEdit(QWidget *parent = nullptr, int parent_width = 0, int parent_height = 0);
	void updateIcon(const QString &);

protected:
	void focusInEvent(QFocusEvent *event);
	void focusOutEvent(QFocusEvent *event);
	void hideEvent(QHideEvent *event);
	bool event(QEvent * event);

Q_SIGNALS:
	void focusIn(void);
	void focusOut(void);
	void hideWindow(void);
	void selectItem(void);
	void moveFocusPrevNext(bool);

private:
	QPropertyAnimation	*m_zoom_animation;
	QAction			*m_searchAction;
};

class SearchBar : public QWidget
{
	Q_OBJECT
public:
	SearchBar(QWidget *parent = nullptr, int width = 0, int height = 0);

private:
	LineEdit		*m_search_edit;

Q_SIGNALS:
	void moveFocusPrevNext(bool);
	void selectItem(void);
	void hideWindow(void);
	void textChanged(const QString &);
};

#endif // SEARCHBAR_H
