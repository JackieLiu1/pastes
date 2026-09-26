#ifndef CARDSWIPE_H
#define CARDSWIPE_H

#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QWidget>

class PasteItem;
class QPropertyAnimation;

/* The GUI thread captures a card once. Drag and animation frames only
 * transform that snapshot; they never retain clipboard data. */
class CardSwipeOverlay : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(qreal offset READ offset WRITE setOffset)

public:
	explicit CardSwipeOverlay(QWidget *parent);
	bool begin(PasteItem *card);
	void setOffset(qreal offset);
	qreal offset(void) const { return m_offset; }
	bool ready(void) const { return m_offset >= m_threshold; }
	void release(bool remove);
	void cancel(void);
	PasteItem *sourceCard(void) const;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QPointer<PasteItem> m_card;
	QPixmap m_snapshot;
	QRectF m_origin;
	QPropertyAnimation *m_animation;
	qreal m_offset = 0;
	qreal m_threshold = 80;
	qreal m_release_offset = 0;
	qreal m_end_offset = 0;
	bool m_removing = false;
};

#endif
