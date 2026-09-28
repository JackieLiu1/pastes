#include "ui/elasticscroll.h"

#include <QEvent>
#include <QListWidget>
#include <QScrollBar>
#include <QtMath>

ElasticScrollController::ElasticScrollController(QListWidget *list) : QObject(list), m_list(list)
{
	setObjectName("ElasticScrollController");
	m_timer.setTimerType(Qt::PreciseTimer);
	m_timer.setInterval(16);
	m_wheel_end.setSingleShot(true);
	m_wheel_end.setInterval(120);
	QObject::connect(&m_timer, &QTimer::timeout, this, &ElasticScrollController::advance);
	QObject::connect(&m_wheel_end, &QTimer::timeout, this, &ElasticScrollController::endWheel);
	QObject::connect(list->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
		/* Keyboard navigation and scrollToItem take ownership immediately. */
		if (!m_applying) this->cancel();
	});
	QObject::connect(list->horizontalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
		if (!m_applying) this->cancel();
	});
	list->installEventFilter(this);
	m_viewport_origin = list->viewport()->pos();
	m_position = list->horizontalScrollBar()->value();
}

qreal ElasticScrollController::limit(void) const
{
	return m_list ? qBound(qreal(42), m_list->viewport()->width()*0.12, qreal(88)) : 42;
}

qreal ElasticScrollController::bounded(qreal position) const
{
	if (!m_list) return 0;
	const QScrollBar *bar = m_list->horizontalScrollBar();
	return qBound(qreal(bar->minimum()), position, qreal(bar->maximum()));
}

qreal ElasticScrollController::overshoot(void) const
{
	return this->bounded(m_position)-m_position;
}

qreal ElasticScrollController::rubber(qreal position) const
{
	const qreal edge = this->bounded(position);
	const qreal distance = position-edge;
	const qreal size = this->limit();
	return edge+distance*0.55*size/(size+qAbs(distance)*0.55);
}

qreal ElasticScrollController::rawPosition(void) const
{
	const qreal edge = this->bounded(m_position);
	const qreal distance = m_position-edge;
	const qreal size = this->limit();
	return edge+distance*size/(0.55*qMax(qreal(1), size-qAbs(distance)));
}

void ElasticScrollController::synchronize(void)
{
	if (m_mode != Mode::Idle || !m_list) return;
	m_position = m_list->horizontalScrollBar()->value();
	m_viewport_origin = m_list->viewport()->pos();
}

void ElasticScrollController::apply(void)
{
	if (!m_list) return;
	m_applying = true;
	m_list->horizontalScrollBar()->setValue(qRound(this->bounded(m_position)));
	/* The list clips the translated viewport at both edges. Its children
	 * remain live widgets, so pointer coordinates still identify the right card. */
	m_list->viewport()->move(m_viewport_origin+QPoint(qRound(this->overshoot()), 0));
	m_applying = false;
}

void ElasticScrollController::beginDrag(void)
{
	if (!m_list || !m_list->count()) return;
	this->synchronize();
	m_timer.stop();
	m_wheel_end.stop();
	m_momentum_return = false;
	m_drag_origin = this->rawPosition();
	m_velocity = 0;
	m_mode = Mode::Drag;
	m_input_clock.start();
}

void ElasticScrollController::dragTo(qreal distance)
{
	if (m_mode != Mode::Drag) return;
	this->moveTo(this->rubber(m_drag_origin-distance));
}

void ElasticScrollController::moveTo(qreal next)
{
	if (qAbs(next-m_position) < 0.01) return;
	const qreal seconds = qMax(qreal(0.008), m_input_clock.nsecsElapsed()/1e9);
	const qreal velocity = qBound(qreal(-2200), (next-m_position)/seconds, qreal(2200));
	const qreal blend = 1-qExp(-seconds/0.035);
	m_velocity += (velocity-m_velocity)*blend;
	m_input_clock.restart();
	m_position = next;
	this->apply();
}

void ElasticScrollController::releaseDrag(bool coast)
{
	if (m_mode != Mode::Drag) return;
	if (!coast || m_input_clock.elapsed() > 90) m_velocity = 0;
	if (qAbs(this->overshoot()) > 0.1) {
		m_target = this->bounded(m_position);
		m_mode = Mode::Spring;
	} else if (qAbs(m_velocity) > 30) {
		m_mode = Mode::Coast;
	} else {
		this->cancel();
		return;
	}
	this->startMotion();
}

void ElasticScrollController::wheel(qreal distance, bool pixels, Qt::ScrollPhase phase)
{
	if (!m_list || !m_list->count()) { this->cancel(); return; }
	this->synchronize();
	if (phase == Qt::ScrollEnd) { this->endWheel(); return; }
	if (pixels && phase == Qt::ScrollMomentum) {
		m_wheel_end.stop();
		/* The fingers have lifted. Native inertia can still browse within
		 * the row, but must not keep holding or pulling a stretched edge. */
		if (m_momentum_return || (m_mode != Mode::Pixels && m_mode != Mode::Momentum))
			return;
		if (qAbs(this->overshoot()) > 0.1) { this->endWheel(); return; }
		m_mode = Mode::Momentum;
		this->moveTo(this->rubber(m_position+distance));
		if (qAbs(this->overshoot()) > 0.1) this->endWheel();
		return;
	}
	const QScrollBar *bar = m_list->horizontalScrollBar();
	m_momentum_return = false;
	if (pixels) {
		if (m_mode != Mode::Pixels || phase == Qt::ScrollBegin) {
			m_wheel_raw = this->rawPosition();
			m_velocity = 0;
			m_input_clock.start();
		}
		m_timer.stop();
		m_mode = Mode::Pixels;
		m_wheel_raw += distance;
		this->moveTo(this->rubber(m_wheel_raw));
	} else if (distance != 0) {
		if (m_mode != Mode::Wheel) m_wheel_raw = this->rawPosition();
		m_mode = Mode::Wheel;
		m_wheel_raw = qBound(bar->minimum()-this->limit()*2,
			m_wheel_raw+distance, bar->maximum()+this->limit()*2);
		m_target = this->rubber(m_wheel_raw);
		this->startMotion();
	}
	/* A phased touchpad can pause while the fingers remain on it. Only
	 * devices without release phases need the inactivity fallback. */
	if (phase == Qt::NoScrollPhase) m_wheel_end.start();
	else m_wheel_end.stop();
}

void ElasticScrollController::endWheel(void)
{
	m_wheel_end.stop();
	if (m_mode == Mode::Pixels || m_mode == Mode::Momentum) {
		if (qAbs(this->overshoot()) <= 0.1) { this->cancel(); return; }
		if (m_input_clock.elapsed() > 90) m_velocity = 0;
		/* Use the mouse release spring, then reject the rest of this
		 * momentum stream even after the spring has come to rest. */
		m_momentum_return = true;
		m_target = this->bounded(m_position);
		m_mode = Mode::Spring;
		this->startMotion();
	} else if (m_mode == Mode::Wheel) {
		m_target = this->bounded(m_target);
		m_mode = Mode::Spring;
		this->startMotion();
	}
}

void ElasticScrollController::startMotion(void)
{
	if (m_timer.isActive()) return;
	m_frame_clock.start();
	m_timer.start();
}

void ElasticScrollController::advance(void)
{
	if (!m_list || !m_list->isVisible()) { this->cancel(); return; }
	qreal remaining = qMin(qreal(0.05), m_frame_clock.nsecsElapsed()/1e9);
	m_frame_clock.restart();
	/* Small integration steps keep the spring stable when painting is late. */
	while (remaining > 0) {
		const qreal seconds = qMin(qreal(0.008), remaining);
		if (m_mode == Mode::Coast) {
			m_velocity *= qExp(-5.5*seconds);
			m_position += m_velocity*seconds;
			if (qAbs(this->overshoot()) > 0) {
				m_target = this->bounded(m_position);
				m_mode = Mode::Spring;
			}
		} else {
			const qreal damping = m_mode == Mode::Wheel ? 32 : 25;
			m_velocity += ((m_target-m_position)*300-damping*m_velocity)*seconds;
			m_position += m_velocity*seconds;
		}
		const qreal edge = this->bounded(m_position);
		if (qAbs(m_position-edge) > this->limit()) {
			m_position = edge+qBound(-this->limit(), m_position-edge, this->limit());
			m_velocity = 0;
		}
		remaining -= seconds;
	}
	this->apply();
	if (m_mode == Mode::Coast ? qAbs(m_velocity) < 8 :
		(qAbs(m_target-m_position) < 0.3 && qAbs(m_velocity) < 8)) {
		if (m_mode != Mode::Coast) m_position = m_target;
		this->apply();
		m_velocity = 0;
		m_timer.stop();
		if (m_mode != Mode::Wheel || !m_wheel_end.isActive()) m_mode = Mode::Idle;
	}
}

void ElasticScrollController::cancel(void)
{
	m_timer.stop();
	m_wheel_end.stop();
	m_velocity = 0;
	if (m_list) {
		m_position = m_list->horizontalScrollBar()->value();
		m_list->viewport()->move(m_viewport_origin);
	}
	m_mode = Mode::Idle;
	m_momentum_return = false;
}

bool ElasticScrollController::eventFilter(QObject *object, QEvent *event)
{
	if (object == m_list && (event->type() == QEvent::Resize || event->type() == QEvent::Hide))
		this->cancel();
	return QObject::eventFilter(object, event);
}
