#include "pasteitembarnner.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>
#include <QPainter>

#include <QDebug>

Barnner::Barnner(QWidget *parent) : QWidget(parent),
	m_icon(new QLabel(this)),
	m_text(new QLabel(this)),
	m_time(new QLabel(this))
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
	hboxlayout->setSpacing(6);
	hboxlayout->setContentsMargins(12, 6, 12, 4);
	this->setLayout(hboxlayout);
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
	const int pixels = qRound(20*ratio);
	QPixmap icon = m_pixmap.scaled(pixels, pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	icon.setDevicePixelRatio(ratio);
	m_icon->setPixmap(icon);
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
