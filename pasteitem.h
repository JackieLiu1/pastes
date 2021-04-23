#ifndef PASTEITEM_H
#define PASTEITEM_H

#include "pasteitemcontext.h"
#include "pasteitembarnner.h"

#include <QListWidgetItem>
#include <QWidget>
#include <QResizeEvent>
#include <QLabel>
#include <QByteArray>
#include <QGraphicsDropShadowEffect>
#include <QMimeData>
#include <QDebug>

static inline QMimeData *dup_mimedata(const QMimeData *mimeData)
{
	QMimeData *mime = new QMimeData;

	for (auto formats : mimeData->formats()) {
		mime->setData(formats, mimeData->data(formats));
	}

	if (mimeData->hasImage()) {
		QImage image = qvariant_cast<QImage>(mimeData->imageData());
		mime->setImageData(image);
	}

	return mime;
}

struct ItemData : QObjectUserData
{
	QMimeData	*mimeData;
	QPixmap		icon;
	QByteArray	md5;

	/* The time of data create */
	QDateTime	time;
};
Q_DECLARE_METATYPE(ItemData);

class PasteItem : public QWidget
{
	Q_OBJECT
public:
	explicit PasteItem(QWidget *parent = nullptr, QListWidgetItem *item = nullptr);
	void setImage(QImage &);
	void setPlainText(QString);
	void setRichText(QString richText, QString plainText);
	bool setUrls(QList<QUrl> &);
	void setIcon(QPixmap);
	void setTime(QDateTime &);
	void copyData(void);

	const QString &text(void)
	{
		return m_text;
	}

	QListWidgetItem *widgetItem(void)
	{
		return m_listwidget_item;
	}

protected:
	void resizeEvent(QResizeEvent *event);
	void mouseDoubleClickEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);

private:
	QWidget				*m_frame;
	QGraphicsDropShadowEffect	*m_frame_effect;

	/* barnner and context */
	Barnner				*m_barnner;
	StackedWidget			*m_context;

	/* scroll list widget item */
	QListWidgetItem			*m_listwidget_item;

	/* text for search */
	QString				m_text;

Q_SIGNALS:
	void hideWindow(void);
};

#endif // PASTEITEM_H
