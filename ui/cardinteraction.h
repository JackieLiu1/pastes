#ifndef PASTES_CARDINTERACTION_H
#define PASTES_CARDINTERACTION_H

#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include <QPoint>
#include "ui/pasteitem.h"

class QWidget;
class QListWidget;
class CardSwipeOverlay;
class CardReflowOverlay;
class ElasticScrollController;

/* Owns only pointer gesture state. History mutation is a view command;
 * animations own snapshots and never retain clipboard payloads. */
class CardInteractionController final : public QObject
{
	Q_OBJECT
public:
	CardInteractionController(QWidget *window, QListWidget *list,
		CardSwipeOverlay *swipe, CardReflowOverlay *reflow, ElasticScrollController *scroll);
	bool handleEvent(QObject *object, QEvent *event);
	void reset(bool cancelSwipe = true, bool cancelReflow = true, bool cancelScroll = true);
	void clearClick(void) { m_lastClicked.clear(); }
	bool isPressed(QWidget *widget) const;

signals:
	void deleteRequested(PasteItem *card);

private:
	QWidget *m_window;
	QListWidget *m_list;
	CardSwipeOverlay *m_swipe;
	CardReflowOverlay *m_reflow;
	ElasticScrollController *m_scroll;
	QPointer<PasteItem> m_pressed;
	QPointer<PasteItem> m_lastClicked;
	QElapsedTimer m_lastClickTime;
	QPoint m_pressPosition;
	bool m_mouseDown = false;
	bool m_mouseMoved = false;
	enum class PointerGesture { Pending, Browse, Dismiss, Cancelled };
	PointerGesture m_gesture = PointerGesture::Pending;
};

#endif
