#include <qsystemdetection.h>

#ifdef Q_OS_LINUX
#include <gio/gdesktopappinfo.h>
#endif

#include <QResizeEvent>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QPair>
#include <QList>
#include <QMimeDatabase>
#include <QDebug>

#include "pasteitemcontext.h"

TextFrame::TextFrame(QWidget *parent) : QLabel(parent),
	m_mask_label(new QLabel(this))
{
	this->setObjectName("ContextTextFrame");
	this->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	this->setWordWrap(true);

	this->m_mask_label->setAlignment(Qt::AlignCenter);
	this->m_mask_label->setContentsMargins(0, 3, 0, 3);
}

TextFrame::~TextFrame()
{
	delete m_mask_label;
}

void TextFrame::setMaskFrameText(QString s)
{
	this->m_mask_label->setText(s);
	this->m_mask_label->show();
}

void TextFrame::setBackgroundColor(QString colorName)
{
	this->setStyleSheet(QString("background-color: %1;").arg(colorName));
}

void TextFrame::resizeEvent(QResizeEvent *event)
{
	this->m_mask_label->setGeometry(0, this->height()-LABEL_HEIGHT,
					this->width(), LABEL_HEIGHT);

	QLabel::resizeEvent(event);
}

PixmapFrame::PixmapFrame(QWidget *parent) : TextFrame(parent)
{
	this->setObjectName("ContextPixmapFrame");
	this->setAlignment(Qt::AlignCenter);
}

void PixmapFrame::resizeEvent(QResizeEvent *event)
{
	if (!m_pixmap.isNull()) {
		this->setPixmap(m_pixmap.scaled(event->size(),
						Qt::KeepAspectRatio,
						Qt::SmoothTransformation));
	}

	TextFrame::resizeEvent(event);
}

FileFrame::FileFrame(QWidget *parent) : TextFrame(parent)
{}

FileFrame::~FileFrame()
{
	for (auto pair : this->m_labels) {
		QLabel *label = pair.first;
		delete label;
	}
}

#ifdef Q_OS_WIN
QPixmap pixmapFromShellImageList(int iImageList, const SHFILEINFO &info)
{
	QPixmap result;
	// For MinGW:
	static const IID iID_IImageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x9, 0x50}};

	IImageList *imageList = nullptr;
	if (FAILED(SHGetImageList(iImageList, iID_IImageList, reinterpret_cast<void **>(&imageList))))
		return result;

	HICON hIcon = 0;
	if (SUCCEEDED(imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &hIcon))) {
		result = QtWin::fromHICON(hIcon);
		DestroyIcon(hIcon);
	}

	return result;
}
#endif

QIcon FileFrame::getIcon(const QString &uri)
{
	QString icon_name;

#ifdef Q_OS_LINUX
	if (uri.endsWith(".desktop")) {
		auto _desktop_file = g_desktop_app_info_new_from_filename(uri.toUtf8().constData());
		auto _icon_string = g_desktop_app_info_get_string(_desktop_file, "Icon");
		QIcon icon = QIcon::fromTheme(_icon_string, QIcon::fromTheme("text-x-generic"));
		g_free(_icon_string);
		g_object_unref(_desktop_file);
		return icon;
	} else {
		auto file = g_file_new_for_path(uri.toLocal8Bit());
		auto info = g_file_query_info(file,
					      G_FILE_ATTRIBUTE_THUMBNAIL_PATH ","
					      G_FILE_ATTRIBUTE_THUMBNAILING_FAILED ","
					      G_FILE_ATTRIBUTE_STANDARD_ICON,
					      G_FILE_QUERY_INFO_NONE,
					      nullptr,
					      nullptr);
		if (!G_IS_FILE_INFO (info))
			return QIcon();
		GIcon *g_icon = g_file_info_get_icon (info);
		//do not unref the GIcon from info.
		if (G_IS_ICON(g_icon)) {
			const gchar* const* icon_names = g_themed_icon_get_names(G_THEMED_ICON (g_icon));
			if (icon_names)
				icon_name = QString (*icon_names);
		}

		g_object_unref(info);
		g_object_unref(file);
		return QIcon::fromTheme(icon_name, QIcon::fromTheme("text-x-generic"));
	}
#endif
#ifdef Q_OS_WIN
	if (!uri.isEmpty()) {
		const QString nativeName = QDir::toNativeSeparators(uri);
		const wchar_t *sourceFileC = reinterpret_cast<const wchar_t *>(nativeName.utf16());

		SHFILEINFO  info;
		if(SHGetFileInfo(sourceFileC,
				 0,
				 &info,
				 sizeof(info),
				 SHGFI_SYSICONINDEX| SHGFI_ICON |  SHGFI_LARGEICON))
		{
			QIcon icon;

			const QPixmap extraLarge = pixmapFromShellImageList(0x4, info);
			icon.addPixmap(extraLarge);

			return icon;
		}
	}

#endif
	return QIcon();
}

void FileFrame::setUrls(QList<QUrl> &urls)
{
	for (int i = 0; i < urls.count() && i < 3; i++) {
		QPixmap pixmap;
		auto url = urls.at(i);
		QMimeDatabase db;
		QMimeType mime = db.mimeTypeForUrl(url);
		if (mime.name().startsWith("image/")) {
			pixmap = QPixmap(url.toLocalFile());
		} else {
			auto icon = this->getIcon(url.toLocalFile());
			pixmap = icon.pixmap(256, 256);
		}
		QLabel *label = new QLabel(this);
		label->setAttribute(Qt::WA_TranslucentBackground);
		QPair<QLabel *, QPixmap> pair(label, pixmap);
		this->m_labels.push_back(pair);
	}
}

void FileFrame::resizeEvent(QResizeEvent *event)
{
	if (!this->m_labels.isEmpty()) {
		int width = this->width() - 40;
		int height = this->height() - 80;
		int label_size = std::min(width, height);
		int start_x = (this->width() - label_size)/2;
		int start_y = (this->height() - label_size)/2;

		for (auto pair : this->m_labels) {
			QLabel *label = pair.first;
			QPixmap pixmap = pair.second;
			label->setGeometry(start_x, start_y, label_size, label_size);
			label->setPixmap(pixmap.scaled(label_size, label_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
		}

		if (this->m_labels.count() == 2) {
			this->m_labels[0].first->move(this->m_labels[0].first->pos()-QPoint(10, 10));
			this->m_labels[1].first->move(this->m_labels[1].first->pos()+QPoint(10, 10));
			this->m_labels[1].first->raise();
		} else if (this->m_labels.count() == 3) {
			this->m_labels[0].first->move(this->m_labels[1].first->pos()-QPoint(20, 20));
			this->m_labels[1].first->raise();
			this->m_labels[2].first->move(this->m_labels[1].first->pos()+QPoint(20, 20));
			this->m_labels[2].first->raise();
		}
	}

	if (!this->m_filename.isEmpty()) {
		QFontMetrics font(this->font());
		QString filename = this->m_filename;
		QFileInfo fileinfo(this->m_filename);
		QString basename = fileinfo.completeBaseName();
		QString dirname = fileinfo.absoluteDir().path() + "/";

		if (!fileinfo.suffix().isEmpty())
			basename += "." + fileinfo.suffix();
		int basename_size = font.width(basename);
		if (basename_size > this->width() - 20) {
			basename = font.elidedText(basename, Qt::ElideLeft, this->width() - 20);
			this->setMaskFrameText("<font color=black>" + basename + "</font>");
		} else {
			int dirname_size = font.width(dirname);
			if (dirname_size > this->width() - 20 - basename_size) {
				 dirname = font.elidedText(dirname, Qt::ElideLeft, this->width() - 20 - basename_size);
			}

			this->setMaskFrameText(dirname + "<font color=black>" + basename + "</font>");
		}
	}

	TextFrame::resizeEvent(event);
}

StackedWidget::StackedWidget(QWidget *parent) : QStackedWidget(parent),
	m_pixmap_frame(new PixmapFrame(this)),
	m_text_frame(new TextFrame(this)),
	m_richtext_frame(new TextFrame(this)),
	m_file_frame(new FileFrame(this))
{
	this->setObjectName("Context");
	this->addWidget(m_pixmap_frame);
	this->addWidget(m_text_frame);
	this->addWidget(m_richtext_frame);
	this->addWidget(m_file_frame);
}

StackedWidget::~StackedWidget()
{
	delete m_pixmap_frame;
	delete m_text_frame;
	delete m_richtext_frame;
	delete m_file_frame;
}

void StackedWidget::setPixmap(QPixmap &pixmap)
{
	m_pixmap_frame->setStorePixmap(pixmap);
	this->setCurrentIndex(StackedWidget::IMAGE);

	QString s = QString("%1x%2 ").arg(pixmap.width()).arg(pixmap.height()) + QObject::tr("px");
	m_pixmap_frame->setMaskFrameText(s);
}

void StackedWidget::setText(QString &s)
{
	if (QColor::isValidColor(s)) {
		m_text_frame->setBackgroundColor(s);
		m_text_frame->setMaskFrameText(s);
	} else {
		m_text_frame->setText(s);
		m_text_frame->setIndent(4);
		m_text_frame->setMaskFrameText(QString("%1 ").arg(s.count()) + QObject::tr("characters"));
	}

	this->setCurrentIndex(StackedWidget::TEXT);
}

void StackedWidget::setRichText(QString &richText, QString &plainText)
{
	if (QColor::isValidColor(plainText.simplified().trimmed())) {
		m_richtext_frame->setBackgroundColor(plainText);
		m_richtext_frame->setMaskFrameText(plainText);
	} else {
		m_richtext_frame->setText(richText);
		m_richtext_frame->setTextFormat(Qt::RichText);
		m_richtext_frame->setMaskFrameText(QString("%1 ").arg(plainText.count()) + QObject::tr("characters"));
	}

	this->setCurrentIndex(StackedWidget::RICHTEXT);
}

void StackedWidget::setUrls(QList<QUrl> &urls)
{
	m_file_frame->setUrls(urls);

	if (urls.count() > 1)
		m_file_frame->setMaskFrameText(QObject::tr("MultiPath"));
	else {
		m_file_frame->setFilename(urls[0].toLocalFile());
	}

	this->setCurrentIndex(StackedWidget::URLS);
}
