#include "cardswipe.h"
#include "pasteitem.h"

#include <QApplication>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTransform>

CardSwipeOverlay::CardSwipeOverlay(QWidget *parent) : QWidget(parent, Qt::Tool |
	Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint |
	Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
	m_animation(new QPropertyAnimation(this, "offset", this))
{
	setObjectName("CardSwipeOverlay");
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	setFocusPolicy(Qt::NoFocus);
	QObject::connect(m_animation, &QPropertyAnimation::finished, this, &CardSwipeOverlay::cancel);
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
	 * bounds. Keep its backing store confined to the upward flight corridor. */
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
	m_removing = remove;
	m_release_offset = m_offset;
	m_end_offset = remove ? qMax(m_offset+260, m_origin.height()+140) : 0;
	/* Deletion commits at mouse release. The outgoing snapshot is purely
	 * visual, so later input or clipboard updates cannot cancel the deletion. */
	if (remove) m_card.clear();
	m_animation->setDuration(remove ? 320 : 200);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	m_animation->setStartValue(m_offset);
	m_animation->setEndValue(m_end_offset);
	m_animation->start();
}

void CardSwipeOverlay::cancel(void)
{
	m_animation->stop();
	if (m_card) m_card->endSwipe();
	m_card.clear();
	m_snapshot = QPixmap();
	m_offset = 0;
	m_removing = false;
	hide();
}

void CardSwipeOverlay::paintEvent(QPaintEvent *)
{
	if (m_snapshot.isNull()) return;
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	const bool dark = qApp->property("pastesDark").toBool();
	const qreal progress = m_removing ? qBound(qreal(0),
		(m_offset-m_release_offset)/qMax(qreal(1), m_end_offset-m_release_offset), qreal(1)) : 0;
	if (!m_removing) {
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

	const qreal tilt = qMin(qreal(5), (m_removing ? m_release_offset : m_offset)/m_threshold*4);
	const qreal angle = m_removing ? tilt+(28-tilt)*progress : tilt;
	QTransform transform;
	/* Toss a rigid card along an outward arc instead of compressing its
	 * height. Dragging remains attached to the pointer until release. */
	transform.translate(m_origin.center().x()+72*progress, m_origin.center().y()-m_offset);
	transform.rotate(-angle);
	transform.translate(-m_origin.width()/2, -m_origin.height()/2);
	painter.setWorldTransform(transform);
	const qreal fade = qMax(qreal(0), (progress-0.32)/0.68);
	painter.setOpacity(1-fade*fade);
	painter.drawPixmap(QRectF(QPointF(), m_origin.size()), m_snapshot, QRectF(m_snapshot.rect()));
}
