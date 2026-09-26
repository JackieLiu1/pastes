#include "cardreflow.h"
#include "pasteitem.h"

#include <QHash>
#include <QListWidget>
#include <QPainter>
#include <QPropertyAnimation>

CardReflowOverlay::CardReflowOverlay(QWidget *parent) : QWidget(parent),
	m_animation(new QPropertyAnimation(this, "progress", this))
{
	setObjectName("CardReflowOverlay");
	setAttribute(Qt::WA_TransparentForMouseEvents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TranslucentBackground);
	setFocusPolicy(Qt::NoFocus);
	m_animation->setDuration(280);
	m_animation->setEasingCurve(QEasingCurve::OutCubic);
	QObject::connect(m_animation, &QPropertyAnimation::finished, this, &CardReflowOverlay::cancel);
	hide();
}

void CardReflowOverlay::prepare(QListWidget *list, QWidget *removed)
{
	/* Repeated deletion continues from the last rendered positions, rather
	 * than snapping to the previous animation's destinations. */
	QHash<PasteItem *, QPointF> positions;
	for (const MovingCard &entry : m_cards) {
		if (entry.card && !entry.snapshot.isNull())
			positions.insert(entry.card.data(), entry.start+(entry.end-entry.start)*m_progress);
	}
	this->cancel();
	if (!list || !list->isVisible()) return;
	m_list = list;
	list->doItemsLayout();
	const QRect visible = list->viewport()->rect();
	for (int i = 0; i < list->count(); ++i) {
		QListWidgetItem *item = list->item(i);
		if (item->isHidden()) continue;
		auto *card = qobject_cast<PasteItem *>(list->itemWidget(item));
		if (!card || card == removed) continue;
		const QRect rect = list->visualItemRect(item);
		/* Include one incoming card on each side when the scroll range shrinks. */
		if (!visible.adjusted(-rect.width()-list->spacing()*2, 0,
			rect.width()+list->spacing()*2, 0).intersects(rect)) continue;
		MovingCard entry;
		entry.card = card;
		entry.start = positions.value(card, rect.topLeft());
		entry.size = rect.size();
		m_cards.push_back(std::move(entry));
	}
}

void CardReflowOverlay::animate(void)
{
	if (!m_list || m_cards.empty()) { this->cancel(); return; }
	m_list->doItemsLayout();
	QWidget *viewport = m_list->viewport();
	setGeometry(QRect(viewport->mapTo(parentWidget(), QPoint()), viewport->size()));
	bool moving = false;
	for (MovingCard &entry : m_cards) {
		if (!entry.card || entry.card->widgetItem()->isHidden()) continue;
		const QRect end = m_list->visualItemRect(entry.card->widgetItem());
		entry.end = end.topLeft();
		if (entry.start == entry.end || entry.size != end.size()) continue;
		if (!rect().intersects(QRectF(entry.start, entry.size).united(QRectF(end)).toAlignedRect())) continue;
		entry.snapshot = entry.card->beginSwipe();
		moving |= !entry.snapshot.isNull();
	}
	if (!moving) { this->cancel(); return; }
	m_progress = 0;
	show();
	raise();
	m_animation->setStartValue(qreal(0));
	m_animation->setEndValue(qreal(1));
	m_animation->start();
}

void CardReflowOverlay::cancel(void)
{
	m_animation->stop();
	for (const MovingCard &entry : m_cards) {
		if (entry.card && !entry.snapshot.isNull()) entry.card->endSwipe();
	}
	m_cards.clear();
	m_list.clear();
	m_progress = 0;
	hide();
}

void CardReflowOverlay::setProgress(qreal progress)
{
	m_progress = progress;
	update();
}

void CardReflowOverlay::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	for (const MovingCard &entry : m_cards) {
		if (!entry.card || entry.snapshot.isNull()) continue;
		const QPointF position = entry.start+(entry.end-entry.start)*m_progress;
		painter.drawPixmap(QRectF(position, entry.size), entry.snapshot, QRectF(entry.snapshot.rect()));
	}
}
