#ifndef ELASTICSCROLL_H
#define ELASTICSCROLL_H

#include <QElapsedTimer>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>

class QListWidget;

/* Move the real viewport for elastic travel: cards retain their normal
 * coordinates and hit testing, without a second set of rendered snapshots. */
class ElasticScrollController : public QObject
{
	Q_OBJECT

public:
	explicit ElasticScrollController(QListWidget *list);
	void beginDrag(void);
	void dragTo(qreal distance);
	void releaseDrag(bool coast = true);
	void wheel(qreal distance, bool pixels, Qt::ScrollPhase phase);
	void cancel(void);
	qreal position(void) const { return m_position; }
	qreal overshoot(void) const;
	bool active(void) const { return m_mode != Mode::Idle; }

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	enum class Mode { Idle, Drag, Pixels, Wheel, Coast, Spring };
	void synchronize(void);
	void apply(void);
	void startMotion(void);
	void advance(void);
	void endWheel(void);
	qreal limit(void) const;
	qreal bounded(qreal position) const;
	qreal rubber(qreal position) const;
	qreal rawPosition(void) const;
	QPointer<QListWidget> m_list;
	QTimer m_timer;
	QTimer m_wheel_end;
	QElapsedTimer m_frame_clock;
	QElapsedTimer m_input_clock;
	QPoint m_viewport_origin;
	Mode m_mode = Mode::Idle;
	qreal m_position = 0;
	qreal m_target = 0;
	qreal m_velocity = 0;
	qreal m_drag_origin = 0;
	qreal m_wheel_raw = 0;
	bool m_applying = false;
};

#endif
