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
	void setActionsVisible(bool visible) { m_time->setVisible(!visible); }

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

private:
	QLabel		*m_icon;
	/* The type text for Barnner */
	QLabel		*m_text;
	/* label for show date time */
	QLabel		*m_time;

	/* icon data */
	QPixmap		m_pixmap;
	/* date time */
	QDateTime	m_datetime;
};

#endif // BARNNER_H
