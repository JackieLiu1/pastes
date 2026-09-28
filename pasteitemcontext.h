#ifndef PASTEITEMCONTEXT_H
#define PASTEITEMCONTEXT_H

#include <QLabel>
#include <QPixmap>
#include <QWidget>
#include <QStackedWidget>
#include <QPainter>
#include <QStyleOption>
#include "roundedwidgets.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <shellapi.h>
#include <comdef.h>
#include <commctrl.h>
#include <objbase.h>
#include <commoncontrols.h>
#endif

#define LABEL_HEIGHT	30

#ifdef Q_OS_WIN
/* Replaces QtWinExtras (removed in Qt6): converts a native HICON to a QPixmap */
QPixmap pixmapFromHICON(HICON icon);
QPixmap pixmapFromShellImageList(int iImageList, const SHFILEINFO &info);
#endif

class TextFrame : public RoundedLabel
{
public:
	TextFrame(QWidget *parent = nullptr);

	void setMaskFrameText(QString);
	void setBackgroundColor(QString);

protected:
	void resizeEvent(QResizeEvent *event);

private:
	QLabel	*m_mask_label;
};

class PixmapFrame : public TextFrame
{
public:
	PixmapFrame(QWidget *parent = nullptr);

	void setStorePixmap(QPixmap pixmap)
	{
		m_pixmap = pixmap;
		/* force a rescale on the next paint/resize */
		m_scaled_size = QSize();
		m_scaled_pixmap = QPixmap();
		this->update();
	}

private:
	QPixmap		m_pixmap;
	QPixmap		m_scaled_pixmap;
	QSize		m_scaled_size;
	qreal		m_scaled_ratio = 0;
	void updatePreviewPixmap(void);

protected:
	void resizeEvent(QResizeEvent *event);
	void paintEvent(QPaintEvent *event) override;
};

class FileFrame : public TextFrame
{
public:
	FileFrame(QWidget *parent = nullptr);
	~FileFrame();

	QIcon getIcon(const QString &uri);
	bool setUrls(QList<QUrl> &);

	void setFilename(QString filename)
	{
		this->m_filename = filename;
	}

protected:
	void resizeEvent(QResizeEvent *event);
	void paintEvent(QPaintEvent *event) override;

#ifdef Q_OS_WIN
	static QIcon getFileIcon(const QString &filename);
	static QIcon getDirIcon(const QString &filename);
	static QIcon getExecutableIcon(const QString &filename);
#endif

private:
	void updatePreviewPixmaps(void);
	QList<QPair<QLabel *, QPixmap>> m_labels;
	QString				m_filename;
	/* Cache by logical size and display scale to skip redundant resizes. */
	int				m_last_label_size = -1;
	qreal			m_last_label_ratio = 0;
};

class StackedWidget : public QStackedWidget
{
public:
	StackedWidget(QWidget *parent = nullptr);
	~StackedWidget();

	void setPixmap(QPixmap &);
	void setText(QString &);
	void setRichText(QString &richText, QString &plainText);
	bool setUrls(QList<QUrl> &);

private:
	PixmapFrame	*m_pixmap_frame;
	TextFrame	*m_text_frame;
	TextFrame	*m_richtext_frame;
	FileFrame	*m_file_frame;
};

#endif // PASTEITEMCONTEXT_H
