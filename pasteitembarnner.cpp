#include "pasteitembarnner.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>

#include <QDebug>

Barnner::Barnner(QWidget *parent) : QWidget(parent),
	m_icon(new QLabel(this)),
	m_text(new QLabel(this)),
	m_time(new QLabel(this))
{
	this->setObjectName("Barnner");
	this->setAttribute(Qt::WA_StyledBackground, true);

	this->m_icon->setScaledContents(false);
	this->m_text->setStyleSheet(this->m_text->styleSheet()+"font-size: 15px; font-weight: 600;");
	this->m_time->setStyleSheet(this->m_time->styleSheet()+"font-size: 11px; color: rgba(255, 255, 255, 0.92);");

	QVBoxLayout *vboxlayout = new QVBoxLayout();
	vboxlayout->setSpacing(3);
	vboxlayout->addStretch();
	vboxlayout->setContentsMargins(20, 0, 0, 0);
	vboxlayout->addWidget(this->m_text);
	vboxlayout->addWidget(this->m_time);
	vboxlayout->addStretch();

	QHBoxLayout *hboxlayout = new QHBoxLayout();
	hboxlayout->addLayout(vboxlayout);
	hboxlayout->addStretch();
	hboxlayout->addWidget(this->m_icon);
	hboxlayout->setSpacing(0);
	hboxlayout->setContentsMargins(0, 0, 0, 0);
	this->setLayout(hboxlayout);
}

void Barnner::setBackground(QRgb rgb)
{
	/* setStyleSheet() reparses the sheet and repaints: skip no-op updates,
	 * resizeEvent used to call this for every event. */
	if (m_has_background && rgb == m_background)
		return;

	m_background = rgb;
	m_has_background = true;
	/* translucent tint: the card surface shows through, and the white
	 * title text stays readable on any source color */
	QString s = QString("background-color: rgba(%1, %2, %3, 90);")
				.arg(qRed(rgb))
				.arg(qGreen(rgb))
				.arg(qBlue(rgb));
	this->setStyleSheet(s + "border-top-left-radius: 12px; border-top-right-radius: 12px;");
}

QRgb Barnner::averageColor(QPixmap *pixmap)
{
	float r = 0, g = 0, b = 0;
	QColor color;

	if (!pixmap || pixmap->isNull())
		return qRgb(60, 62, 68);

	QImage image = pixmap->toImage();
	for (int i = 0; i < image.width(); i++) {
		for (int j = 0; j < image.height(); j++) {
			color = QColor(image.pixel(i, j));
			r += color.red();
			g += color.green();
			b += color.blue();
		}
	}

	int count = image.width() * image.height();
	r /= count;
	g /= count;
	b /= count;

	/* Darken the average so the white title text stays readable whatever
	 * the source color (a white icon must not produce a white banner) */
	return qRgb((int)(r * 0.45 + 8), (int)(g * 0.45 + 8), (int)(b * 0.45 + 12));
}

void Barnner::resizeEvent(QResizeEvent *event)
{
	QSize size = event->size();

	if (!this->m_pixmap.isNull()) {
		m_icon->setFixedHeight(size.height());
		m_icon->setFixedWidth(size.height()*0.8);
		QPixmap mp = this->m_pixmap.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		m_icon->setAlignment(Qt::AlignCenter);

		this->setBackground(m_avg_color);
		this->m_icon->setPixmap(mp);
	}

	QWidget::resizeEvent(event);
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
