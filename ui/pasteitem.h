#ifndef PASTEITEM_H
#define PASTEITEM_H

#include "ui/pasteitemcontext.h"
#include "ui/pasteitembarnner.h"
#include "core/clipboarddata.h"
#include "core/itemdata.h"

#include <QListWidgetItem>
#include <QWidget>
#include <QResizeEvent>
#include <QLabel>
#include <QByteArray>
#include <QGraphicsDropShadowEffect>
#include <QMimeData>
#include <QDebug>

class PasteItem : public QWidget
{
	Q_OBJECT
public:
	explicit PasteItem(QWidget *parent = nullptr, QListWidgetItem *item = nullptr);
	bool setEntry(const HistoryEntry &entry, bool loaded = false);
	const HistoryEntry &entry(void) const { return m_entry; }
	void setImage(const QImage &, const QSize &originalSize = QSize());
	bool setImage(const QMimeData *mime);
	void setPlainText(QString);
	void setRichText(QString richText, QString plainText);
	bool setUrls(QList<QUrl> &);
	void setIcon(QPixmap);
	void setTime(QDateTime &);
	void updateFavorite(void);
	void setFavoriteMoves(bool left, bool right);
	void copyData(bool plainText = false, bool paste = true);
	void setQuickPasteNumber(int number);
	void setSelected(bool selected);
	void setPressed(bool pressed);
	QPixmap beginSwipe(void);
	void endSwipe(void);

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
	void mouseDoubleClickEvent(QMouseEvent *event);
	void keyPressEvent(QKeyEvent *event);
	void contextMenuEvent(QContextMenuEvent *event);

private:
	void setCardKind(const char *kind);
	bool setPathPreview(const QString &text);
	QWidget				*m_frame;
	QGraphicsDropShadowEffect	*m_frame_effect;

	/* barnner and context */
	Barnner				*m_barnner;
	StackedWidget			*m_context;
	QLabel				*m_quick_paste_number;

	/* scroll list widget item */
	QListWidgetItem			*m_listwidget_item;

	/* text for search */
	QString				m_text;
	HistoryEntry m_entry;
	bool m_moveLeft = false;
	bool m_moveRight = false;

Q_SIGNALS:
	void hideWindow(void);
	void copyRequested(HistoryEntry entry, bool plainText, bool paste);
	void moveFocusPrevNext(bool prev, bool wrap);
	void previewRequested(void);
	void deleteRequested(void);
	void favoriteRequested(void);
	void renameFavoriteRequested(void);
	void moveFavoriteRequested(bool left);
};

#endif // PASTEITEM_H
