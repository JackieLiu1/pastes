#include "pasteitembarnner.h"
#include "roundedwidgets.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>

#include <QDebug>

Barnner::Barnner(QWidget *parent) : QWidget(parent),
	m_icon(new QLabel(this)),
	m_text(new RoundedLabel(RoundedRole::TypeBadge, this)),
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
	m_icon->setVisible(!m_pixmap.isNull());
	m_icon->setPixmap(m_pixmap.scaled(20, 20, Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
