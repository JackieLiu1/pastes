#include "cardswipe.h"
#include "pasteitem.h"

#include <QApplication>
#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPolygonF>
#include <QPropertyAnimation>
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
	 * bounds. Keep its backing store confined to the drag and glow corridor. */
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
		this->prepareSeam();
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
	/* Reverse a live fold at its current phase. After the fold, start at the
	 * seam so restoring a card never replays the deletion's shutdown flash. */
	const bool reverse = dismissalId && dismissalId == m_dismissal_id &&
		m_removing && !m_restoring && !m_snapshot.isNull();
	const qreal progress = reverse ? qMin(m_dismissal, qreal(0.60)) : qreal(0.60);
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
	this->prepareSeam();
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

void CardSwipeOverlay::prepareSeam(void)
{
	const qreal camera = qMax(qreal(550), m_origin.height()*2.5);
	const qreal depth = 38+m_origin.height()*0.24;
	m_line_center = QPointF(m_origin.center().x(), m_origin.bottom()-m_release_offset-36);
	m_line_width = (m_origin.width()-8)*camera/(camera+depth);
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
	const QColor halo(qApp->property("pastesDark").toBool() ? "#8EDCC4" : "#399D82");
	const QColor core("#FFF9E9");
	if (progress >= 0.55 && progress < 0.86) {
		/* Like a CRT switching off, contract the bright seam toward its
		 * center. Undo starts at the full seam and only unfolds the card. */
		const qreal collapse = m_restoring ? 0 : QEasingCurve(QEasingCurve::InCubic).
			valueForProgress(qBound(qreal(0), (progress-0.60)/0.26, qreal(1)));
		const qreal light = qMin(qreal(1), (progress-0.55)/0.07);
		const qreal halfWidth = qMax(qreal(0.7), m_line_width*(1-collapse)/2);
		const QPointF left = m_line_center-QPointF(halfWidth, 0);
		const QPointF right = m_line_center+QPointF(halfWidth, 0);
		QLinearGradient gradient(left, right);
		QColor transparent = halo;
		transparent.setAlpha(0);
		gradient.setColorAt(0, transparent);
		gradient.setColorAt(0.18, halo);
		gradient.setColorAt(0.5, core);
		gradient.setColorAt(0.82, halo);
		gradient.setColorAt(1, transparent);
		painter.setOpacity(light*0.22);
		painter.setPen(QPen(QBrush(gradient), 6, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(left, right);
		painter.setOpacity(light);
		painter.setPen(QPen(QBrush(gradient), 1.5, Qt::SolidLine, Qt::RoundCap));
		painter.drawLine(left, right);
	}
	if (m_restoring || progress < 0.80) return;
	const qreal flash = qBound(qreal(0), (progress-0.80)/0.06, qreal(1));
	const qreal fade = 1-QEasingCurve(QEasingCurve::OutCubic).valueForProgress(
		qBound(qreal(0), (progress-0.86)/0.14, qreal(1)));
	QRadialGradient glow(m_line_center, 9);
	glow.setColorAt(0, core);
	QColor soft = halo;
	soft.setAlpha(100);
	glow.setColorAt(0.28, soft);
	soft.setAlpha(0);
	glow.setColorAt(1, soft);
	painter.setOpacity(flash*fade);
	painter.setPen(Qt::NoPen);
	painter.setBrush(glow);
	painter.drawEllipse(m_line_center, 9, 9);
	painter.setBrush(core);
	painter.drawEllipse(m_line_center, 1.5*fade, 1.5*fade);
}
