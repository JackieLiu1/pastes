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

struct ItemData
{
	QMimeData	*mimeData;
	/* QImage (not QPixmap) so the value can safely cross threads */
	QImage		icon;
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
	void copyData(bool plainText = false, bool paste = true);
	void setQuickPasteNumber(int number);
	void setSelected(bool selected);
	void setPressed(bool pressed);

	const QString &text(void)
	{
		return m_text;
	}

	QListWidgetItem *widgetItem(void)
	{
		return m_listwidget_item;
	}

protected:
	bool event(QEvent *event);
	void resizeEvent(QResizeEvent *event);
	void enterEvent(QEnterEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);
	void contextMenuEvent(QContextMenuEvent *event);

private:
	void setCardKind(const char *kind);
	void updateActions(void);
	QWidget				*m_frame;
	QGraphicsDropShadowEffect	*m_frame_effect;

	/* barnner and context */
	Barnner				*m_barnner;
	StackedWidget			*m_context;
	QLabel				*m_quick_paste_number;
	QWidget				*m_actions;
	bool				m_hovered = false;

	/* scroll list widget item */
	QListWidgetItem			*m_listwidget_item;

	/* text for search */
	QString				m_text;

Q_SIGNALS:
	void hideWindow(void);
	void copied(void);
	void clipboardUpdated(void);
	void moveFocusPrevNext(bool prev);
	void previewRequested(void);
	void deleteRequested(void);
};

#endif // PASTEITEM_H
