#ifndef BARNNER_H
#define BARNNER_H

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QDateTime>
#include <utility>

class QPushButton;

class Barnner : public QWidget
{
	Q_OBJECT
public:
	explicit Barnner(QWidget *parent = nullptr);

	void setIcon(QPixmap &pixmap);
	void setFavorite(bool favorite);
	void setFavoriteName(const QString &name);


	QPixmap icon(void)
	{
		return m_pixmap;
	}

	void setTitle(QString s)
	{
		m_title = std::move(s);
		updateTitle();
	}

	void setTime(QDateTime &dateTime)
	{
		this->m_datetime = dateTime;
	}

signals:
	void favoriteRequested(void);

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	void showEvent(QShowEvent *) override;
	void paintEvent(QPaintEvent *event) override;

private:
	void updateIconPixmap(void);
	void updateTitle(void);
	QString m_title;
	QString m_favoriteName;
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
