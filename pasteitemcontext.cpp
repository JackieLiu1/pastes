#include "platform/fileicon.h"
#include "filepreview.h"

#include <algorithm>

#include <QResizeEvent>
#include <QFileInfo>
#include <QDir>
#include <QPixmap>
#include <QPair>
#include <QList>
#include <QMimeDatabase>
#include <QGraphicsDropShadowEffect>
#include <QDebug>

#include "pasteitemcontext.h"

TextFrame::TextFrame(QWidget *parent) : RoundedLabel(RoundedRole::Content, parent),
	m_mask_label(new RoundedLabel(RoundedRole::Footer, this))
{
	this->setObjectName("ContextTextFrame");
	this->setAlignment(Qt::AlignTop | Qt::AlignLeft);
	this->setWordWrap(true);

	this->m_mask_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	this->m_mask_label->setContentsMargins(14, 3, 38, 3);
}

void TextFrame::setMaskFrameText(QString s)
{
	this->m_mask_label->setText(s);
	this->m_mask_label->show();
}

void TextFrame::setBackgroundColor(QString colorName)
{
	const QColor color(colorName);
	this->setProperty("swatchColor", color);
	this->update();
	this->m_mask_label->setStyleSheet(qGray(color.rgb()) < 145 ?
		"color: #FFFFFF; background: transparent;" : "color: #24372D; background: transparent;");
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
	TextFrame::resizeEvent(event);
	this->updatePreviewPixmap();
}

void PixmapFrame::updatePreviewPixmap(void)
{
	/* Reserve the footer: a solid information row must not cover the image.
	 * Cache by usable size and DPI so repeated paints/resizes only blit. */
	const QSize available(qMax(1, this->width()-12), qMax(1, this->height()-LABEL_HEIGHT-12));
	const qreal ratio = this->devicePixelRatioF();
	if (!m_pixmap.isNull() && (m_scaled_size != available || m_scaled_ratio != ratio)) {
		m_scaled_size = available;
		m_scaled_ratio = ratio;
		m_scaled_pixmap = m_pixmap.scaled(QSize(qRound(available.width()*ratio), qRound(available.height()*ratio)),
			Qt::KeepAspectRatio, Qt::SmoothTransformation);
		m_scaled_pixmap.setDevicePixelRatio(ratio);
	}
}

void PixmapFrame::paintEvent(QPaintEvent *)
{
	this->updatePreviewPixmap();
	if (m_scaled_pixmap.isNull()) return;
	const QRectF preview = QRectF(this->rect()).adjusted(6, 6, -6, -LABEL_HEIGHT-6);
	if (preview.isEmpty()) return;
	const QSizeF size = QSizeF(m_scaled_pixmap.size())/m_scaled_pixmap.devicePixelRatioF();
	QPainter painter(this);
	painter.setClipRect(preview);
	painter.drawPixmap(preview.center()-QPointF(size.width()/2, size.height()/2), m_scaled_pixmap);
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

bool FileFrame::setUrls(QList<QUrl> &urls)
{
	bool ret = false;

	for (int i = 0; i < urls.count() && i < 3; i++) {
		QPixmap pixmap;
		auto url = urls.at(i);

		QFileInfo fileinfo(url.toLocalFile());
		if (!fileinfo.exists())
			continue;
		else
			ret |= true;

		QMimeDatabase db;
		QMimeType mime = db.mimeTypeForUrl(url);
		if (mime.name().startsWith("image/")) {
			pixmap = QPixmap::fromImage(FilePreview::loadImage(url, 512));
		}
		if (pixmap.isNull()) {
			auto icon = Platform::fileIcon(url.toLocalFile());
			pixmap = icon.pixmap(256, 256);
		}
		QLabel *label = new QLabel(this);
		label->setAttribute(Qt::WA_TranslucentBackground);
		/* The shadow effect only has to be attached once, not on every resize */
		QGraphicsDropShadowEffect *shadow = new QGraphicsDropShadowEffect(label);
		shadow->setOffset(0, 0);
		shadow->setColor(QColor(0, 0, 0, 110));
		shadow->setBlurRadius(12);
		label->setGraphicsEffect(shadow);
		QPair<QLabel *, QPixmap> pair(label, pixmap);
		this->m_labels.push_back(pair);
	}

	m_last_label_size = -1;
	this->update();
	return ret;
}

void FileFrame::updatePreviewPixmaps(void)
{
	if (m_labels.isEmpty())
		return;
	const int size = m_labels.first().first->width();
	const qreal ratio = this->devicePixelRatioF();
	if (size <= 0 || (m_last_label_size == size && m_last_label_ratio == ratio))
		return;
	m_last_label_size = size;
	m_last_label_ratio = ratio;
	const int pixels = qRound(size*ratio);
	for (const auto &pair : m_labels) {
		QPixmap pixmap = pair.second.scaled(pixels, pixels,
			Qt::KeepAspectRatio, Qt::SmoothTransformation);
		pixmap.setDevicePixelRatio(ratio);
		pair.first->setPixmap(pixmap);
	}
}

void FileFrame::paintEvent(QPaintEvent *event)
{
	this->updatePreviewPixmaps();
	TextFrame::paintEvent(event);
}

void FileFrame::resizeEvent(QResizeEvent *event)
{
	if (!this->m_labels.isEmpty()) {
		int width = this->width() - 60;
		int height = this->height() - 80;
		int label_size = std::min(width, height);
		int start_x = (this->width() - label_size)/2;
		int start_y = (this->height() - label_size)/2;

		for (auto pair : this->m_labels) {
			QLabel *label = pair.first;
			label->setGeometry(start_x, start_y, label_size, label_size);
		}

		/* Cache the physical pixel size, including the display's scale. */
		this->updatePreviewPixmaps();

		if (this->m_labels.count() == 2) {
			this->m_labels[0].first->move(this->m_labels[0].first->pos()-QPoint(15, 10));
			this->m_labels[1].first->move(this->m_labels[1].first->pos()+QPoint(15, 10));
			this->m_labels[1].first->raise();
		} else if (this->m_labels.count() == 3) {
			this->m_labels[0].first->move(this->m_labels[1].first->pos()-QPoint(30, 20));
			this->m_labels[1].first->raise();
			this->m_labels[2].first->move(this->m_labels[1].first->pos()+QPoint(30, 20));
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
		int basename_size = font.horizontalAdvance(basename);
		if (basename_size > this->width() - 20) {
			basename = font.elidedText(basename, Qt::ElideLeft, this->width() - 20);
			this->setMaskFrameText("<span>" + basename.toHtmlEscaped() + "</span>");
		} else {
			int dirname_size = font.horizontalAdvance(dirname);
			if (dirname_size > this->width() - 20 - basename_size) {
				 dirname = font.elidedText(dirname, Qt::ElideLeft, this->width() - 20 - basename_size);
			}

			this->setMaskFrameText("<span>" + (dirname + basename).toHtmlEscaped() + "</span>");
		}
	}

	TextFrame::resizeEvent(event);
}

StackedWidget::StackedWidget(QWidget *parent) : QStackedWidget(parent)
{
	this->setObjectName("Context");
}

StackedWidget::~StackedWidget()
{}

void StackedWidget::setPixmap(QPixmap &pixmap)
{
	PixmapFrame *pixmap_frame = new PixmapFrame(this);

	pixmap_frame->setStorePixmap(pixmap);
	QString s = QString("%1x%2 ").arg(pixmap.width()).arg(pixmap.height()) + QObject::tr("px");
	pixmap_frame->setMaskFrameText(s);

	this->addWidget(pixmap_frame);
}

void StackedWidget::setText(QString &s)
{
	TextFrame *text_frame = new TextFrame(this);

	if (QColor::isValidColor(s)) {
		text_frame->setBackgroundColor(s);
		text_frame->setMaskFrameText(s);
	} else {
		text_frame->setText(s);
		text_frame->setIndent(4);
		text_frame->setMaskFrameText(QString("%1 ").arg(s.count()) + QObject::tr("characters"));
	}

	this->addWidget(text_frame);
}

void StackedWidget::setRichText(QString &richText, QString &plainText)
{
	TextFrame *richtext_frame = new TextFrame(this);

	if (QColor::isValidColor(plainText.simplified().trimmed())) {
		richtext_frame->setBackgroundColor(plainText);
		richtext_frame->setMaskFrameText(plainText);
	} else {
		richtext_frame->setText(richText);
		richtext_frame->setTextFormat(Qt::RichText);
		richtext_frame->setMaskFrameText(QString("%1 ").arg(plainText.count()) + QObject::tr("characters"));
	}

	this->addWidget(richtext_frame);
}

bool StackedWidget::setUrls(QList<QUrl> &urls)
{
	FileFrame *file_frame = new FileFrame(this);

	if (!file_frame->setUrls(urls)) {
		delete file_frame;
		return false;
	}

	if (urls.count() > 1)
		file_frame->setMaskFrameText(QObject::tr("MultiPath"));
	else {
		file_frame->setFilename(urls[0].toLocalFile());
	}

	this->addWidget(file_frame);

	return true;
}
