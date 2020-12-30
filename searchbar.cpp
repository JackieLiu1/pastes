#include "searchbar.h"

#include <QHBoxLayout>
#include <QResizeEvent>

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QAction>
#include <QDebug>

LineEdit::LineEdit(QWidget *parent, int parent_width, int parent_height) : QLineEdit(parent),
	m_zoom_animation(new QPropertyAnimation(this, "minimumWidth"))
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

	QAction *searchAction = new QAction(this);
	searchAction->setIcon(QIcon(":/resources/search.png"));
	this->addAction(searchAction, QLineEdit::TrailingPosition);
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

PushButton::PushButton(QWidget *parent) : QPushButton(parent),
	m_label(new QLabel(this)),
	m_pixmap(QPixmap(":/resources/search.png"))
{
	this->setAttribute(Qt::WA_StyledBackground);
	this->setFocusPolicy(Qt::ClickFocus);
	m_label->setAlignment(Qt::AlignCenter);
}

void PushButton::updatePixmap(void)
{
	if (!m_pixmap.isNull()) {
		QPixmap pixmap = m_pixmap.scaled(this->size()*0.8, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		m_label->setPixmap(pixmap);
	}
}

void PushButton::setPixmap(QPixmap pixmap)
{
	m_pixmap = pixmap;
	this->updatePixmap();
}

void PushButton::resizeEvent(QResizeEvent *event)
{
	m_label->setGeometry(QRect(0, 0, event->size().width(), event->size().height()));
	this->updatePixmap();
	QPushButton::resizeEvent(event);
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
