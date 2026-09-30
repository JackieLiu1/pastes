#ifndef ROUNDEDWIDGETS_H
#define ROUNDEDWIDGETS_H

#include <QColor>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QWidget>

class QPainter;
class QFocusEvent;
class QMouseEvent;

enum class RoundedRole { Panel, Card, Search, HistoryBadge, Number, MenuButton, Content, Footer,
	Preview, PreviewContent };

struct RoundedSurfaceStyle
{
	QColor fill;
	QColor border;
	qreal radius = 0;
	qreal borderWidth = 0;
	int corners = 0; // 0: all, 1: top only, 2: bottom only
	bool gradient = false;

	bool operator==(const RoundedSurfaceStyle &other) const;
};

/* GUI-thread-owned cache. The 2x render is rebuilt only for a changed size,
 * DPI or visual state; ordinary repaint/animation frames just blit it. */
class RoundedSurface
{
public:
	void paint(QWidget *widget, RoundedRole role, QPainter &painter);

private:
	QPixmap m_pixmap;
	QSize m_size;
	qreal m_ratio = 0;
	RoundedSurfaceStyle m_style;
};

class RoundedWidget : public QWidget
{
public:
	RoundedWidget(RoundedRole role, QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	RoundedRole m_role;
	RoundedSurface m_surface;
};

class RoundedLabel : public QLabel
{
public:
	RoundedLabel(RoundedRole role, QWidget *parent = nullptr);
	RoundedLabel(const QString &text, RoundedRole role, QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	RoundedRole m_role;
	RoundedSurface m_surface;
};

class RoundedButton : public QPushButton
{
public:
	explicit RoundedButton(QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	RoundedSurface m_surface;
};

class DialogCloseButton : public QPushButton
{
public:
	explicit DialogCloseButton(QWidget *parent = nullptr);

protected:
	void paintEvent(QPaintEvent *event) override;
	void focusInEvent(QFocusEvent *event) override;
	void focusOutEvent(QFocusEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;

private:
	bool m_keyboardFocus = false;
};

#endif
