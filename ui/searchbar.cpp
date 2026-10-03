#include "ui/searchbar.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QPainter>
#include <QDebug>

LineEdit::LineEdit(QWidget *parent, int parent_width, int parent_height) : QLineEdit(parent)
{
	/* Parenting the action dispatches ChildAdded to event(). Initialize all
	 * member state before creating a child which can reenter this widget. */
	m_searchAction = new QAction(this);
	this->setFocusPolicy(Qt::ClickFocus);
	this->setContextMenuPolicy(Qt::NoContextMenu);
	this->setFixedHeight(parent_height);
	this->setFixedWidth(parent_width);
	this->setFrame(false);

	this->updateIcon();
	this->addAction(m_searchAction, QLineEdit::TrailingPosition);
	QObject::connect(m_searchAction, &QAction::triggered, this, [this]() {
		if (m_composing) qApp->inputMethod()->reset();
		this->clear();
		m_composing = false;
	});
}

void LineEdit::updateIcon(void)
{
	const qreal ratio = this->devicePixelRatioF();
	QPixmap pixmap(qRound(18*ratio), qRound(18*ratio));
	pixmap.setDevicePixelRatio(ratio);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(this->palette().color(QPalette::Text), 1.4, Qt::SolidLine, Qt::RoundCap));
	if (this->text().isEmpty()) {
		painter.drawEllipse(QRectF(3, 3, 8, 8));
		painter.drawLine(QPointF(10, 10), QPointF(15, 15));
	} else {
		painter.drawLine(QPointF(5, 5), QPointF(13, 13));
		painter.drawLine(QPointF(5, 13), QPointF(13, 5));
	}
	painter.end();
	this->m_searchAction->setIcon(QIcon(pixmap));
}

void LineEdit::changeEvent(QEvent *event)
{
	QLineEdit::changeEvent(event);
	if (event->type() == QEvent::PaletteChange) {
		QPalette colors = this->palette();
		QColor placeholder = colors.color(QPalette::Text);
		placeholder.setAlpha(128);
		if (!qApp->property("pastesDark").toBool())
			placeholder = QColor("#58615D");
		/* Do not fade light-theme hints into the paper-colored field. */
		if (colors.color(QPalette::PlaceholderText) != placeholder) {
			colors.setColor(QPalette::PlaceholderText, placeholder);
			this->setPalette(colors);
		}
		this->updateIcon();
	}
}

void LineEdit::paintEvent(QPaintEvent *event)
{
	{ QPainter painter(this); m_surface.paint(this, RoundedRole::Search, painter); }
	QLineEdit::paintEvent(event);
}

void LineEdit::focusInEvent(QFocusEvent *event)
{
	emit this->focusIn();
	QLineEdit::focusInEvent(event);
}

void LineEdit::focusOutEvent(QFocusEvent *event)
{
	emit this->focusOut();
	QLineEdit::focusOutEvent(event);
	m_composing = false;
}

void LineEdit::inputMethodEvent(QInputMethodEvent *event)
{
	m_composing = !event->preeditString().isEmpty();
	QLineEdit::inputMethodEvent(event);
}

bool LineEdit::event(QEvent *event)
{
	/* Candidate selection belongs to the input method, including keys which
	 * otherwise paste a card, navigate the list or dismiss the panel. */
	if (m_composing && event->type() == QEvent::ShortcutOverride) {
		event->accept();
		return true;
	}
	if (m_composing && event->type() == QEvent::KeyPress) {
		QLineEdit::event(event);
		event->accept(); // Do not propagate an unhandled key to MainFrame.
		return true;
	}
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
		case Qt::Key_Right:
		case Qt::Key_Down:
			emit this->moveFocusPrevNext(false, false);
			return true;
		}
	}

	return QLineEdit::event(event);
}

void LineEdit::hideEvent(QHideEvent *event)
{
	this->clear();
	m_composing = false;
	QLineEdit::hideEvent(event);
}

SearchBar::SearchBar(QWidget *parent, int width, int height) : QWidget(parent)
{
	this->setAttribute(Qt::WA_TranslucentBackground);
	this->setAttribute(Qt::WA_StyledBackground);
	this->setObjectName("SearchBar");
	this->setFixedSize(width, height);

	this->m_search_edit = new LineEdit(this, width, height);
	m_search_edit->setPlaceholderText(QObject::tr("Type to search"));
	m_search_edit->setTextMargins(14, 0, 0, 0);

	QObject::connect(m_search_edit, &LineEdit::hideWindow, this, &SearchBar::hideWindow);
	QObject::connect(m_search_edit, &LineEdit::textChanged, this, [this](const QString &text) {
		this->m_search_edit->updateIcon();
		emit this->textChanged(text);
	});
	QObject::connect(m_search_edit, &LineEdit::selectItem, this, &SearchBar::selectItem);
	QObject::connect(m_search_edit, &LineEdit::selectPlainTextItem, this,
			 &SearchBar::selectPlainTextItem);
	QObject::connect(m_search_edit, &LineEdit::moveFocusPrevNext,
			 this, &SearchBar::moveFocusPrevNext);

	QHBoxLayout *layout = new QHBoxLayout();
	layout->addStretch();
	layout->addWidget(this->m_search_edit);
	layout->addStretch();
	layout->setContentsMargins(0, 0, 0, 0);

	this->setLayout(layout);
}
