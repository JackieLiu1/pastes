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

	void setIcon(QPixmap &pixmap)
	{
		m_pixmap = pixmap;
		/* computed once here instead of on every resize */
		m_avg_color = averageColor(&m_pixmap);
	}

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

	void setBackground(QRgb rgb);
	static QRgb averageColor(QPixmap *);

protected:
	void resizeEvent(QResizeEvent *event);
	void showEvent(QShowEvent *);

private:
	QLabel		*m_icon;
	/* The type text for Barnner */
	QLabel		*m_text;
	/* label for show date time */
	QLabel		*m_time;

	/* icon data */
	QPixmap		m_pixmap;
	/* cached average color of the icon */
	QRgb		m_avg_color = qRgb(255, 255, 255);
	/* last color applied, to skip redundant stylesheet updates */
	QRgb		m_background = 0;
	bool		m_has_background = false;
	/* date time */
	QDateTime	m_datetime;
};

#endif // BARNNER_H
