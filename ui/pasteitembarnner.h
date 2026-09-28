#ifndef BARNNER_H
#define BARNNER_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>

class Barnner : public QWidget
{
	Q_OBJECT
public:
	explicit Barnner(QWidget *parent = nullptr);

	void setIcon(QPixmap &pixmap);

	QPixmap icon(void)
	{
		return m_pixmap;
	}

	void setTitle(QString s)
	{
		this->m_text->setText(s);
	}

	void setTime(QDateTime &dateTime)
	{
		this->m_datetime = dateTime;
	}

protected:
	void showEvent(QShowEvent *);
	void paintEvent(QPaintEvent *event) override;

private:
	void updateIconPixmap(void);
	QLabel		*m_icon;
	/* The type text for Barnner */
	QLabel		*m_text;
	/* label for show date time */
	QLabel		*m_time;

	/* icon data */
	QPixmap		m_pixmap;
	qreal		m_scaled_ratio = 0;
	/* date time */
	QDateTime	m_datetime;
};

#endif // BARNNER_H
