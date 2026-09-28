#include "ui/cardinteraction.h"
#include "ui/pasteitem.h"
#include "ui/cardswipe.h"
#include "ui/cardreflow.h"
#include "ui/elasticscroll.h"
#include <QAbstractButton>
#include <QApplication>
#include <QMouseEvent>
#include <QStyleHints>
#include <QWheelEvent>

CardInteractionController::CardInteractionController(QWidget *window, QListWidget *list,
	CardSwipeOverlay *swipe, CardReflowOverlay *reflow, ElasticScrollController *scroll)
	: QObject(list), m_window(window), m_list(list), m_swipe(swipe), m_reflow(reflow), m_scroll(scroll)
{}

bool CardInteractionController::isPressed(QWidget *widget) const
{
	return m_pressed == widget;
}

void CardInteractionController::reset(bool cancelSwipe, bool cancelReflow, bool cancelScroll)
{
	if (this->m_pressed)
		this->m_pressed->setPressed(false);
	this->m_pressed.clear();
	this->m_mouseDown = false;
	this->m_mouseMoved = false;
	this->m_gesture = PointerGesture::Pending;
	if (cancelSwipe && this->m_swipe) this->m_swipe->cancel();
	if (cancelReflow && this->m_reflow) this->m_reflow->cancel();
	if (cancelScroll && this->m_scroll) this->m_scroll->cancel();
	if (QWidget::mouseGrabber() == this->m_list->viewport())
		this->m_list->viewport()->releaseMouse();
	this->m_list->viewport()->unsetCursor();
}

bool CardInteractionController::handleEvent(QObject *object, QEvent *event)
{
	const QEvent::Type type = event->type();
	if (object == m_list->viewport() && type == QEvent::UngrabMouse && m_mouseDown) {
		reset();
		clearClick();
	}
	if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
	    type != QEvent::MouseButtonDblClick && type != QEvent::MouseMove &&
	    type != QEvent::Wheel && type != QEvent::ContextMenu)
		return false;
	QWidget *target = qobject_cast<QWidget *>(object);
	QWidget *viewport = this->m_list->viewport();
	/* Owned menus and preview windows also have a card ancestor, but their
	 * input belongs to their own window. Only browse within the list. */
	if (!target || target->window() != m_window || !(target == this->m_list ||
	    target == viewport || viewport->isAncestorOf(target)))
		return false;
	PasteItem *card = nullptr;
	bool button = false;
	for (QWidget *widget = target; widget && widget != viewport; widget = widget->parentWidget()) {
		button |= qobject_cast<QAbstractButton *>(widget) != nullptr;
		if ((card = qobject_cast<PasteItem *>(widget))) break;
	}
	if (type == QEvent::Wheel) {
		this->reset(true, true, false);
		this->m_lastClicked.clear();
		QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
		const QPoint pixels = wheel->pixelDelta();
		const QPoint angle = wheel->angleDelta();
		const int amount = !pixels.isNull() ? (pixels.x() ? pixels.x() : pixels.y()) :
			(angle.x() ? angle.x() : angle.y())*48*qApp->styleHints()->wheelScrollLines()/120;
		this->m_scroll->wheel(-amount, !pixels.isNull() || wheel->phase() != Qt::NoScrollPhase,
			wheel->phase());
		wheel->accept();
		return true;
	}
	if (type == QEvent::ContextMenu) {
		this->reset();
		if (card) {
			this->m_list->setCurrentItem(card->widgetItem());
			card->setFocus(Qt::MouseFocusReason);
		}
		return false;
	}
	QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
	if (button && !this->m_mouseDown) return false;
	if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
		if (mouse->button() != Qt::LeftButton) {
			this->reset();
			if (card && mouse->button() == Qt::RightButton) {
				this->m_list->setCurrentItem(card->widgetItem());
				card->setFocus(Qt::MouseFocusReason);
			}
			return false;
		}
		this->reset(true, true, false);
		if (type == QEvent::MouseButtonDblClick && card && this->m_lastClicked == card &&
		    this->m_lastClickTime.isValid() && this->m_lastClickTime.elapsed() <= QApplication::doubleClickInterval()) {
			this->m_lastClicked.clear();
			card->copyData(mouse->modifiers().testFlag(Qt::ShiftModifier));
			return true;
		}
		this->m_mouseDown = true;
		this->m_pressPosition = mouse->globalPosition().toPoint();
		this->m_scroll->beginDrag();
		this->m_pressed = card;
		if (card) card->setPressed(true);
		return true;
	}
	if (type == QEvent::MouseMove) {
		/* A wheel or hide can cancel our press while Qt still reports a
		 * held button. Never let its default drag-selection handle an
		 * unowned move. Browsing and hovering must not select a card. */
		if (!this->m_mouseDown) {
			mouse->accept();
			return true;
		}
		const QPoint delta = mouse->globalPosition().toPoint()-this->m_pressPosition;
		if (!(mouse->buttons() & Qt::LeftButton)) {
			this->reset();
			this->m_lastClicked.clear();
			return true;
		}
		if (delta.manhattanLength() >= QApplication::startDragDistance()) {
			this->m_mouseMoved = true;
			this->m_lastClicked.clear();
			if (this->m_pressed) this->m_pressed->setPressed(false);
		}
		/* Lock direction once the initial movement is clear. A horizontal
		 * browse cannot turn into deletion when the pointer later moves up. */
		if (this->m_gesture == PointerGesture::Pending && this->m_mouseMoved) {
			if (qAbs(delta.x()) > qAbs(delta.y())*1.2) {
				this->m_gesture = PointerGesture::Browse;
				viewport->grabMouse(Qt::ClosedHandCursor);
			} else if (-delta.y() > qAbs(delta.x())*1.2 && this->m_pressed) {
				this->m_scroll->cancel();
				this->m_list->setCurrentItem(this->m_pressed->widgetItem());
				this->m_pressed->setFocus(Qt::MouseFocusReason);
				if (this->m_swipe->begin(this->m_pressed)) {
					this->m_gesture = PointerGesture::Dismiss;
					viewport->grabMouse(Qt::ClosedHandCursor);
				}
			} else if (delta.y() > qAbs(delta.x())*1.2) {
				this->m_gesture = PointerGesture::Cancelled;
			}
		}
		if (this->m_gesture == PointerGesture::Browse)
			this->m_scroll->dragTo(delta.x());
		else if (this->m_gesture == PointerGesture::Dismiss)
			this->m_swipe->setOffset(-delta.y());
		return true;
	}
	if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
		QPointer<PasteItem> pressed = this->m_pressed;
		if (this->m_mouseDown && this->m_gesture == PointerGesture::Browse) {
			this->m_scroll->dragTo(mouse->globalPosition().x()-this->m_pressPosition.x());
			this->reset(true, true, false);
			this->m_scroll->releaseDrag();
			this->m_lastClicked.clear();
			return true;
		}
		if (this->m_mouseDown && this->m_gesture == PointerGesture::Dismiss && pressed) {
			/* The release position wins even if its final movement produced no
			 * MouseMove. Dragging back below the threshold always cancels. */
			this->m_swipe->setOffset(this->m_pressPosition.y()-mouse->globalPosition().y());
			const bool remove = this->m_swipe->ready();
			this->reset(false);
			this->m_lastClicked.clear();
			this->m_swipe->release(remove);
			if (remove) {
				this->m_list->setCurrentItem(pressed->widgetItem());
				emit deleteRequested(pressed);
			}
			return true;
		}
		const bool click = this->m_mouseDown && !this->m_mouseMoved && pressed &&
			(mouse->globalPosition().toPoint()-this->m_pressPosition).manhattanLength() < QApplication::startDragDistance() &&
			pressed->rect().contains(pressed->mapFromGlobal(mouse->globalPosition().toPoint()));
		this->reset(true, true, false);
		this->m_scroll->releaseDrag(false);
		if (click) {
			this->m_list->setCurrentItem(pressed->widgetItem());
			pressed->setFocus(Qt::MouseFocusReason);
			this->m_lastClicked = pressed;
			this->m_lastClickTime.start();
		} else {
			this->m_lastClicked.clear();
		}
		return true;
	}
	return false;
}
