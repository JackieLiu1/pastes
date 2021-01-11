#include "searchbar.h"

#include <QHBoxLayout>
#include <QResizeEvent>
#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QDebug>

LineEdit::LineEdit(QWidget *parent, int parent_width, int parent_height) : QLineEdit(parent),
	m_zoom_animation(new QPropertyAnimation(this, "minimumWidth")),
	m_searchAction(new QAction(this))
{
	this->setFocusPolicy(Qt::ClickFocus);
	this->setContextMenuPolicy(Qt::NoContextMenu);
	this->setFixedHeight(parent_height);
	this->setMaximumWidth(parent_width-parent_height*1.2);
	this->setMinimumWidth(parent_width/2);

	m_zoom_animation->setDuration(100);
	m_zoom_animation->setStartValue(parent_width/2);
	m_zoom_animation->setEndValue(parent_width-parent_height*1.2);
	QObject::connect(this->m_zoom_animation, &QAbstractAnimation::finished, [parent](void){
		parent->update();
		QWidget *focusWidget = QApplication::focusWidget();
		if (focusWidget)
			focusWidget->update();
	});

	m_searchAction->setIcon(QIcon(":/resources/search.png"));
	this->addAction(m_searchAction, QLineEdit::TrailingPosition);
	QObject::connect(m_searchAction, &QAction::triggered, [this]() {
		this->setText("");
	});
}

void LineEdit::updateIcon(const QString &url)
{
	this->m_searchAction->setIcon(QIcon(url));
}

void LineEdit::focusInEvent(QFocusEvent *event)
{
	m_zoom_animation->setDirection(QAbstractAnimation::Forward);
	m_zoom_animation->start();
	emit this->focusIn();
	QLineEdit::focusInEvent(event);
}

void LineEdit::focusOutEvent(QFocusEvent *event)
{
	m_zoom_animation->setDirection(QAbstractAnimation::Backward);
	m_zoom_animation->start();
	emit this->focusOut();
	QLineEdit::focusOutEvent(event);
}

bool LineEdit::event(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *ke = static_cast<QKeyEvent *>(event);
		switch (ke->key()) {
		case Qt::Key_Return:
		case Qt::Key_Enter:
			emit this->selectItem();
			return true;
		case Qt::Key_Escape:
			emit this->hideWindow();
			return true;
		case Qt::Key_Tab:
			emit this->moveFocusPrevNext(false);
			return true;
		case Qt::Key_Backtab:
			emit this->moveFocusPrevNext(true);
			return true;
		}
	}

	return QLineEdit::event(event);
}

void LineEdit::hideEvent(QHideEvent *event)
{
	this->clear();
	QLineEdit::hideEvent(event);
}

SearchBar::SearchBar(QWidget *parent, int width, int height) : QWidget(parent)
{
	this->setAttribute(Qt::WA_TranslucentBackground);
	this->setAttribute(Qt::WA_StyledBackground);
	this->setObjectName("SearchBar");
	this->setFixedSize(width, height);

	QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect(this);
	effect->setOffset(0, 0);
	effect->setColor(QColor(0, 0, 0, 90));
	effect->setBlurRadius(5);
	this->setGraphicsEffect(effect);

	this->m_search_edit = new LineEdit(this, width, height);
	m_search_edit->setPlaceholderText(QObject::tr("Search"));
	m_search_edit->setTextMargins(10, 0, 0, 0);

	QObject::connect(m_search_edit, &LineEdit::hideWindow, [this](void) {
		emit this->hideWindow();
	});
	QObject::connect(m_search_edit, &LineEdit::textChanged, [this](const QString &text) {
		if (text.isEmpty()) {
			this->m_search_edit->updateIcon(":/resources/search.png");
		} else {
			this->m_search_edit->updateIcon(":/resources/search_clear.png");
		}
		emit this->textChanged(text);
	});
	QObject::connect(m_search_edit, &LineEdit::selectItem, [this](void) {
		emit this->selectItem();
	});
	QObject::connect(m_search_edit, &LineEdit::moveFocusPrevNext, [this](bool prev) {
		emit this->moveFocusPrevNext(prev);
	});

	QHBoxLayout *layout = new QHBoxLayout();
	layout->addStretch();
	layout->addWidget(this->m_search_edit);
	layout->addStretch();
	layout->setContentsMargins(0, 0, 0, 0);

	this->setLayout(layout);
}
