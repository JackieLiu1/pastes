#include "cardswipe.h"
#include "pasteitem.h"

#include <QApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QScreen>
#include <QTransform>
#include <QtMath>

CardSwipeOverlay::CardSwipeOverlay(QWidget *parent) : QWidget(parent, Qt::Tool |
	Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint |
	Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
	m_animation(new QPropertyAnimation(this, "offset", this)),
	m_dismiss_animation(new QPropertyAnimation(this, "dismissal", this))
{
	setObjectName("CardSwipeOverlay");
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setFocusPolicy(Qt::NoFocus);
	QObject::connect(m_animation, &QPropertyAnimation::finished, this, &CardSwipeOverlay::cancel);
	QObject::connect(m_dismiss_animation, &QPropertyAnimation::finished, this, &CardSwipeOverlay::cancel);
	hide();
}

PasteItem *CardSwipeOverlay::sourceCard(void) const
{
	return m_card.data();
}

bool CardSwipeOverlay::begin(PasteItem *card)
{
	this->cancel();
	if (!card || !card->isVisible()) return false;
	m_card = card;
	const QRect globalOrigin(card->mapToGlobal(QPoint()), card->size());
	QScreen *screen = QGuiApplication::screenAt(globalOrigin.center());
	if (!screen) screen = card->screen();
	/* A separate, input-transparent window lets the card cross the panel's
	 * bounds. Keep its backing store confined to the drag and particle corridor. */
	const QRect corridor(globalOrigin.left()-90, screen->geometry().top(),
		globalOrigin.width()+220, globalOrigin.bottom()+50-screen->geometry().top());
	setGeometry(corridor.intersected(screen->geometry()));
	m_origin = QRectF(globalOrigin.translated(-pos()));
	m_threshold = qBound(qreal(140), card->height()*0.65, qreal(200));
	m_snapshot = card->beginSwipe();
	if (m_snapshot.isNull()) {
		this->cancel();
		return false;
	}
	m_offset = 0;
	m_removing = false;
	show();
	raise();
	return true;
}

void CardSwipeOverlay::setOffset(qreal offset)
{
	m_offset = qMax(qreal(0), offset);
	update();
}

void CardSwipeOverlay::release(bool remove)
{
	if (m_snapshot.isNull()) return;
	m_animation->stop();
	m_dismiss_animation->stop();
	m_removing = remove;
	m_release_offset = m_offset;
	m_dismissal = 0;
	/* Deletion commits at mouse release. The outgoing snapshot is purely
	 * visual, so later input or clipboard updates cannot cancel the deletion. */
	if (remove) {
		m_departing_card = m_card;
		m_card.clear();
		++m_dismissal_id;
		this->prepareParticles();
		m_dismiss_animation->setDuration(700);
		m_dismiss_animation->setEasingCurve(QEasingCurve::Linear);
		m_dismiss_animation->setStartValue(qreal(0));
		m_dismiss_animation->setEndValue(qreal(1));
		m_dismiss_animation->start();
		return;
	}
	m_animation->setDuration(200);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	m_animation->setStartValue(m_offset);
	m_animation->setEndValue(qreal(0));
	m_animation->start();
}

quint64 CardSwipeOverlay::dismissalId(PasteItem *card) const
{
	return card && m_departing_card == card && m_removing && !m_restoring ? m_dismissal_id : 0;
}

bool CardSwipeOverlay::restore(PasteItem *card, quint64 dismissalId)
{
	/* An immediate undo reverses the outgoing visual at its current phase.
	 * Later undo uses the same renderer, with a shorter trip above the slot. */
	const bool reverse = dismissalId && dismissalId == m_dismissal_id &&
		m_removing && !m_restoring && !m_snapshot.isNull();
	const qreal progress = reverse ? m_dismissal : 1;
	const qreal lift = reverse ? m_release_offset : qBound(qreal(60), card ? card->height()*0.32 : 0, qreal(110));
	if (!this->begin(card)) return false;
	m_restoring = true;
	m_release_offset = lift;
	if (reverse && progress < 0.04) {
		/* The card is still facing forward; just bring it back down. */
		m_offset = lift;
		m_animation->setDuration(220);
		m_animation->setEasingCurve(QEasingCurve::OutCubic);
		m_animation->setStartValue(lift);
		m_animation->setEndValue(qreal(0));
		m_animation->start();
		return true;
	}
	m_removing = true;
	m_restore_start = progress;
	m_dismissal = progress;
	this->prepareParticles(reverse);
	m_dismiss_animation->setDuration(qMax(180, qRound(660*progress)));
	m_dismiss_animation->setEasingCurve(QEasingCurve::Linear);
	m_dismiss_animation->setStartValue(progress);
	m_dismiss_animation->setEndValue(qreal(0));
	m_dismiss_animation->start();
	return true;
}

void CardSwipeOverlay::cancel(void)
{
	m_animation->stop();
	m_dismiss_animation->stop();
	if (m_card) m_card->endSwipe();
	m_card.clear();
	m_departing_card.clear();
	m_snapshot = QPixmap();
	m_offset = 0;
	m_dismissal = 0;
	m_removing = false;
	m_restoring = false;
	hide();
}

void CardSwipeOverlay::paintEvent(QPaintEvent *)
{
	if (m_snapshot.isNull()) return;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	const bool dark = qApp->property("pastesDark").toBool();
	if (m_removing) {
		this->paintDismissal(painter);
		return;
	}
	if (!m_restoring) {
		const QRectF hint(m_origin.left()+16, m_origin.bottom()-50, m_origin.width()-32, 32);
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(dark ? "#392826" : "#F6E7DF"));
		painter.drawRoundedRect(hint, 10, 10);
		painter.setPen(QColor(ready() ? (dark ? "#F0A799" : "#B95342") : (dark ? "#B5B5B5" : "#758278")));
		QFont font = this->font();
		font.setPixelSize(12);
		painter.setFont(font);
		painter.drawText(hint, Qt::AlignCenter, ready() ? QObject::tr("Release to remove") :
			QObject::tr("Drag up to remove"));
	}

	/* Before release, keep the front face flat and attached to the pointer. */
	painter.drawPixmap(m_origin.translated(0, -m_offset), m_snapshot, QRectF(m_snapshot.rect()));
}

void CardSwipeOverlay::setDismissal(qreal progress)
{
	m_dismissal = qBound(qreal(0), progress, qreal(1));
	update();
}

void CardSwipeOverlay::prepareParticles(bool reuse)
{
	const qreal camera = qMax(qreal(550), m_origin.height()*2.5);
	const qreal depth = 38+m_origin.height()*0.24;
	m_line_center = QPointF(m_origin.center().x(), m_origin.bottom()-m_release_offset-36);
	m_line_width = (m_origin.width()-8)*camera/(camera+depth);
	if (reuse) return;
	const bool dark = qApp->property("pastesDark").toBool();
	const std::array<QColor, 3> colors = dark ? std::array<QColor, 3>{
		QColor("#8EDCC4"), QColor("#F2C879"), QColor("#FFF4D8")} :
		std::array<QColor, 3>{QColor("#399D82"), QColor("#D59D4D"), QColor("#79AFA1")};
	/* Randomness and allocation happen once at release, never per frame. */
	QRandomGenerator *random = QRandomGenerator::global();
	for (size_t i = 0; i < m_particles.size(); ++i) {
		Particle &particle = m_particles[i];
		const qreal angle = random->generateDouble()*2*M_PI;
		const qreal speed = 95+random->generateDouble()*105;
		particle.origin = QPointF((random->generateDouble()-0.5)*m_line_width*0.82, 0);
		particle.velocity = QPointF(qCos(angle)*speed, qSin(angle)*speed-22);
		particle.color = colors[i%colors.size()];
		particle.radius = 1.1+random->generateDouble()*1.1;
	}
}

void CardSwipeOverlay::paintDismissal(QPainter &painter)
{
	const qreal progress = m_dismissal;
	const qreal fold = QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(
		qBound(qreal(0), progress/0.60, qreal(1)));
	const qreal camera = qMax(qreal(550), m_origin.height()*2.5);
	const qreal depth = (38+m_origin.height()*0.24)*fold;
	const qreal angle = qDegreesToRadians(qMin(qreal(89.75), fold*90));
	const qreal lift = m_restoring ? m_release_offset*
		QEasingCurve(QEasingCurve::InOutCubic).valueForProgress(qBound(qreal(0),
			progress/qMin(qreal(0.60), m_restore_start), qreal(1))) : m_release_offset;
	const QPointF hinge(m_origin.center().x(), m_origin.bottom()-lift-36*fold);
	if (progress < 0.60) {
		/* Rotate the top edge away from the viewer around the bottom edge.
		 * Perspective narrows the receding edge; its height converges to a line. */
		auto project = [&](qreal x, qreal y) {
			const qreal distance = m_origin.height()-y;
			const qreal scale = camera/(camera+depth+distance*qSin(angle));
			return hinge+QPointF((x-m_origin.width()/2)*scale, -distance*qCos(angle)*scale);
		};
		const qreal width = m_origin.width(), height = m_origin.height();
		const QPolygonF source{QPointF(0, 0), QPointF(width, 0), QPointF(width, height), QPointF(0, height)};
		const QPolygonF target{project(0, 0), project(width, 0), project(width, height), project(0, height)};
		QTransform transform;
		if (QTransform::quadToQuad(source, target, transform)) {
			painter.save();
			painter.setWorldTransform(transform);
			painter.setOpacity(1-fold*0.20);
			painter.drawPixmap(QRectF(QPointF(), m_origin.size()), m_snapshot, QRectF(m_snapshot.rect()));
			painter.restore();
		}
	}
	if (progress >= 0.55 && progress < 0.86) {
		const qreal light = qMin(qreal(1), (progress-0.55)/0.07)*
			(1-qBound(qreal(0), (progress-0.68)/0.18, qreal(1)));
		const QPointF left = m_line_center-QPointF(m_line_width/2, 0);
		const QPointF right = m_line_center+QPointF(m_line_width/2, 0);
		QLinearGradient gradient(left, right);
		gradient.setColorAt(0, QColor(142, 220, 196, 0));
		gradient.setColorAt(0.18, QColor("#8EDCC4"));
		gradient.setColorAt(0.5, QColor("#FFF4D8"));
		gradient.setColorAt(0.82, QColor("#F2C879"));
		gradient.setColorAt(1, QColor(242, 200, 121, 0));
		painter.setOpacity(light*0.18);
		painter.setPen(QPen(QBrush(gradient), 5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(left, right);
		painter.setOpacity(light);
		painter.setPen(QPen(QBrush(gradient), 1.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(left, right);
	}
	if (progress < 0.67) return;
	const qreal burst = qBound(qreal(0), (progress-0.67)/0.33, qreal(1));
	const qreal time = burst*0.33;
	const qreal alpha = qMin(qreal(1), burst*10)*qPow(1-burst, 1.4);
	for (const Particle &particle : m_particles) {
		const QPointF position = m_line_center+particle.origin+particle.velocity*time+QPointF(0, 55*time*time);
		const qreal radius = particle.radius*(1-burst*0.55);
		painter.setPen(Qt::NoPen);
		painter.setBrush(particle.color);
		painter.setOpacity(alpha*0.14);
		painter.drawEllipse(position, radius*2.4, radius*2.4);
		painter.setOpacity(alpha);
		painter.drawEllipse(position, radius, radius);
		painter.setPen(QPen(particle.color, radius*0.7, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(position-particle.velocity*0.018, position);
	}
}
