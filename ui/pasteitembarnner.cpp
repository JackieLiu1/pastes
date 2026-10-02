#include "ui/pasteitembarnner.h"
#include "ui/sourceiconview.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>

#include <QDebug>

namespace {

class FavoriteButton : public QPushButton
{
public:
	explicit FavoriteButton(QWidget *parent) : QPushButton(parent) {}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.translate((width() - 16)/2.0, (height() - 16)/2.0);
		const QColor color = palette().color(isChecked() || underMouse() || isDown() ?
			QPalette::Highlight : QPalette::ButtonText);
		QPainterPath star;
		star.moveTo(8, 1);
		star.lineTo(10.1, 5.3);
		star.lineTo(14.9, 6);
		star.lineTo(11.4, 9.4);
		star.lineTo(12.2, 14.2);
		star.lineTo(8, 11.9);
		star.lineTo(3.8, 14.2);
		star.lineTo(4.6, 9.4);
		star.lineTo(1.1, 6);
		star.lineTo(5.9, 5.3);
		star.closeSubpath();
		painter.setPen(QPen(color, 1.25, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		if (isChecked()) {
			QColor fill = color;
			fill.setAlpha(48);
			painter.setBrush(fill);
		}
		painter.drawPath(star);
	}
};

}

Barnner::Barnner(QWidget *parent) : QWidget(parent),
	m_icon(new QLabel(this)),
	m_text(new QLabel(this)),
	m_time(new QLabel(this)),
	m_favorite(new FavoriteButton(this))
{
	this->setObjectName("Barnner");
	this->setAttribute(Qt::WA_StyledBackground, true);

	this->m_icon->setScaledContents(false);
	this->m_text->setObjectName("CardType");
	this->m_time->setObjectName("CardTime");
	this->m_icon->setFixedSize(24, 24);
	this->m_icon->setAlignment(Qt::AlignCenter);

	QHBoxLayout *hboxlayout = new QHBoxLayout();
	hboxlayout->addWidget(this->m_icon);
	hboxlayout->addWidget(this->m_text);
	hboxlayout->addStretch();
	hboxlayout->addWidget(this->m_time);
	hboxlayout->addWidget(this->m_favorite);
	m_favorite->setObjectName("FavoriteButton");
	m_favorite->setFixedSize(24, 24);
	m_favorite->setFlat(true);
	m_favorite->setCheckable(true);
	m_favorite->setFocusPolicy(Qt::NoFocus);
	m_favorite->setCursor(Qt::PointingHandCursor);
	m_favorite->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	setFavorite(false);
	connect(m_favorite, &QPushButton::toggled, this, &Barnner::favoriteRequested);
	hboxlayout->setSpacing(6);
	hboxlayout->setContentsMargins(12, 6, 12, 4);
	this->setLayout(hboxlayout);
}

void Barnner::setFavorite(bool favorite)
{
	const QSignalBlocker blocker(m_favorite);
	m_favorite->setChecked(favorite);
	const QString action = favorite ? QObject::tr("Remove from Favorites") : QObject::tr("Add to Favorites");
	m_favorite->setToolTip(action);
	m_favorite->setAccessibleName(action);
}

void Barnner::setIcon(QPixmap &pixmap)
{
	m_pixmap = pixmap;
	m_icon->setToolTip(m_pixmap.isNull() ? QObject::tr("Source unavailable") : QString());
	if (m_pixmap.isNull()) {
		QImage image(40, 40, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QPen(QColor("#81968C"), 2));
		painter.drawRoundedRect(QRectF(5, 7, 30, 26), 4, 4);
		painter.drawLine(QPointF(5, 15), QPointF(35, 15));
		painter.end();
		m_pixmap = QPixmap::fromImage(image);
	}
	m_scaled_ratio = 0;
	this->updateIconPixmap();
}

void Barnner::updateIconPixmap(void)
{
	const qreal ratio = this->devicePixelRatioF();
	if (m_pixmap.isNull() || m_scaled_ratio == ratio)
		return;
	m_scaled_ratio = ratio;
	m_icon->setPixmap(SourceIconView::pixmap(m_pixmap.toImage(), 20, ratio));
}

void Barnner::paintEvent(QPaintEvent *event)
{
	this->updateIconPixmap();
	QWidget::paintEvent(event);
}

void Barnner::showEvent(QShowEvent *event)
{
	if (!this->m_datetime.isNull()) {
		qint64 currntSecs = QDateTime::currentDateTime().toSecsSinceEpoch();
		qint64 createSecs = this->m_datetime.toSecsSinceEpoch();
		qint64 period = currntSecs - createSecs;
		if (period >= 0) {
			int months  = period / (30 * 24 * 60 * 60);
			int days    = period / (24 * 60 * 60);
			int hours   = period / (60 * 60);
			int minutes = period / 60;
			int secs    = period;

			if (months) {
				this->m_time->setText(QString("%1 ").arg(months) + QObject::tr("months ago"));
			} else if (days) {
				this->m_time->setText(QString("%1 ").arg(days) + QObject::tr("days ago"));
			} else if (hours) {
				this->m_time->setText(QString("%1 ").arg(hours) + QObject::tr("hours ago"));
			} else if (minutes) {
				this->m_time->setText(QString("%1 ").arg(minutes) + QObject::tr("minutes ago"));
			} else if (secs > 10) {
				this->m_time->setText(QString("%1 ").arg(secs) + QObject::tr("secs ago"));
			} else {
				this->m_time->setText(QObject::tr("moment ago"));
			}
		}
	}

	QWidget::showEvent(event);
}
