#ifndef BARNNER_H
#define BARNNER_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>

class QPushButton;

class Barnner : public QWidget
{
	Q_OBJECT
public:
	explicit Barnner(QWidget *parent = nullptr);

	void setIcon(QPixmap &pixmap);
	void setFavorite(bool favorite);


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

signals:
	void favoriteRequested(void);

protected:
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *event) override;

private:
	void updateIconPixmap(void);
	QLabel		*m_icon;
	/* The type text for Barnner */
	QLabel		*m_text;
	/* label for show date time */
	QLabel		*m_time;
	QPushButton *m_favorite;

	/* icon data */
	QPixmap		m_pixmap;
	qreal		m_scaled_ratio = 0;
	/* date time */
	QDateTime	m_datetime;
};

#endif // BARNNER_H
