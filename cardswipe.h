#ifndef CARDSWIPE_H
#define CARDSWIPE_H

#include <QPixmap>
#include <QColor>
#include <QPointer>
#include <QRectF>
#include <QWidget>
#include <array>

class PasteItem;
class QPropertyAnimation;
class QPainter;

/* The GUI thread captures a card once. Drag and animation frames only
 * transform that snapshot; they never retain clipboard data. */
class CardSwipeOverlay : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(qreal offset READ offset WRITE setOffset)
	Q_PROPERTY(qreal dismissal READ dismissal WRITE setDismissal)

public:
	explicit CardSwipeOverlay(QWidget *parent);
	bool begin(PasteItem *card);
	void setOffset(qreal offset);
	qreal offset(void) const { return m_offset; }
	qreal dismissal(void) const { return m_dismissal; }
	void setDismissal(qreal progress);
	bool ready(void) const { return m_offset >= m_threshold; }
	void release(bool remove);
	bool restore(PasteItem *card, quint64 dismissalId = 0);
	quint64 dismissalId(PasteItem *card) const;
	void cancel(void);
	PasteItem *sourceCard(void) const;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	void prepareParticles(bool reuse = false);
	void paintDismissal(QPainter &painter);
	struct Particle {
		QPointF origin;
		QPointF velocity;
		QColor color;
		qreal radius;
	};
	std::array<Particle, 36> m_particles;
	QPointer<PasteItem> m_card;
	QPointer<PasteItem> m_departing_card;
	QPixmap m_snapshot;
	QRectF m_origin;
	QPropertyAnimation *m_animation;
	QPropertyAnimation *m_dismiss_animation;
	qreal m_offset = 0;
	qreal m_threshold = 80;
	qreal m_release_offset = 0;
	qreal m_dismissal = 0;
	qreal m_restore_start = 1;
	QPointF m_line_center;
	qreal m_line_width = 0;
	bool m_removing = false;
	bool m_restoring = false;
	quint64 m_dismissal_id = 0;
};

#endif
