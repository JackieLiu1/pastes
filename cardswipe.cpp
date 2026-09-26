#include "cardswipe.h"
#include "pasteitem.h"

#include <QApplication>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTransform>

CardSwipeOverlay::CardSwipeOverlay(QWidget *parent) : QWidget(parent),
	m_animation(new QPropertyAnimation(this, "offset", this))
{
	setObjectName("CardSwipeOverlay");
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
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
	m_origin = QRectF(card->mapTo(parentWidget(), QPoint()), card->size());
	m_threshold = qBound(qreal(64), card->height()*0.32, qreal(112));
	m_snapshot = card->beginSwipe();
	if (m_snapshot.isNull()) {
		this->cancel();
		return false;
	}
	m_offset = 0;
	m_removing = false;
	setGeometry(parentWidget()->rect());
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
	m_end_offset = remove ? qMax(m_offset+180, m_origin.bottom()+30) : 0;
	/* Deletion commits at mouse release. The outgoing snapshot is purely
 * visual, so later input or clipboard updates cannot cancel the deletion. */
	if (remove) m_card.clear();
	m_animation->setDuration(remove ? 260 : 170);
	m_animation->setEasingCurve(remove ? QEasingCurve::InOutCubic : QEasingCurve::OutCubic);
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

	const qreal tilt = qMin(qreal(18), m_offset/m_threshold*14);
	const qreal angle = m_removing ? tilt+(82-tilt)*progress : tilt;
	QTransform transform;
	/* Fold first, then travel upward, so the flip remains visible within
	 * the short bottom panel instead of immediately leaving its bounds. */
	const qreal visualOffset = m_removing ? m_release_offset+
		(m_end_offset-m_release_offset)*progress*progress : m_offset;
	transform.translate(m_origin.center().x(), m_origin.center().y()-visualOffset);
	transform.rotate(-angle, Qt::XAxis);
	transform.scale(1-progress*0.08, 1-progress*0.08);
	transform.translate(-m_origin.width()/2, -m_origin.height()/2);
	painter.setWorldTransform(transform);
	painter.setOpacity(1-progress*progress);
	painter.drawPixmap(QRectF(QPointF(), m_origin.size()), m_snapshot, QRectF(m_snapshot.rect()));
}
