#include "platform/fileicon.h"
#include "ui/filepreview.h"
#include "core/clipboarddata.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QResizeEvent>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QDir>
#include <QPixmap>
#include <QPair>
#include <QList>
#include <QGraphicsDropShadowEffect>
#include <QDebug>
#include <QtMath>

#include "ui/pasteitemcontext.h"

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

void TextFrame::resizeEvent(QResizeEvent *event)
{
	this->m_mask_label->setGeometry(0, this->height()-LABEL_HEIGHT,
					this->width(), LABEL_HEIGHT);

	QLabel::resizeEvent(event);
}

namespace {

constexpr int textPreviewLimit = 256;
constexpr int textPreviewLines = 12;
constexpr int richPreviewLimit = 4096;

QString textPreview(const QString &text)
{
	int length = int(qMin<qsizetype>(text.size(), textPreviewLimit));
	int lines = 1;
	for (int i = 0; i < length; ++i) {
		if (text.at(i) == '\n' && lines++ == textPreviewLines) {
			length = i;
			break;
		}
	}
	if (length == text.size()) return text;
	if (text.at(length-1).isHighSurrogate()) --length;
	return text.left(length) + QChar(0x2026);
}

QColor blend(const QColor &background, const QColor &foreground, qreal amount)
{
	return QColor(qRound(background.red()*(1-amount) + foreground.red()*amount),
		qRound(background.green()*(1-amount) + foreground.green()*amount),
		qRound(background.blue()*(1-amount) + foreground.blue()*amount));
}

qreal luminance(const QColor &color)
{
	/* WCAG relative luminance uses linear sRGB, not the average RGB value. */
	auto linear = [](qreal channel) {
		return channel <= 0.04045 ? channel/12.92 : std::pow((channel+0.055)/1.055, 2.4);
	};
	return 0.2126*linear(color.redF()) + 0.7152*linear(color.greenF()) +
		0.0722*linear(color.blueF());
}

QColor swatchTextColor(const QColor &background)
{
	const qreal light = luminance(background);
	const QColor endpoint = (light+0.05)/0.05 >= 1.05/(light+0.05) ? Qt::black : Qt::white;
	/* Keep a tint/shade of the swatch, strengthening it until normal text
	 * reaches 4.5:1 contrast. One of the two endpoints always satisfies it. */
	for (int percent = 45; percent < 100; ++percent) {
		const QColor foreground = blend(background, endpoint, percent/100.0);
		const qreal foregroundLight = luminance(foreground);
		if ((qMax(light, foregroundLight)+0.05)/(qMin(light, foregroundLight)+0.05) >= 4.5)
			return foreground;
	}
	return endpoint;
}

}

ColorFrame::ColorFrame(const QString &text, QWidget *parent)
	: QLabel(text.trimmed(), parent), m_color(text.trimmed())
{
	this->setObjectName("ContextColorFrame");
	this->setTextFormat(Qt::PlainText);
	this->setAlignment(Qt::AlignCenter);
	this->setProperty("swatchColor", m_color);
}

void ColorFrame::paintEvent(QPaintEvent *)
{
	/* Alpha colors are displayed over the card's themed surface. */
	QColor surface(qApp->property("pastesDark").toBool() ? "#282828" : "#FFFDF8");
	QWidget *card = this->parentWidget();
	while (card && card->objectName() != "PasteItemFrame") card = card->parentWidget();
	if (card && card->property("pressed").toBool())
		surface = qApp->property("pastesDark").toBool() ? surface.lighter(108) : surface.darker(103);
	const QColor background = blend(surface, m_color, m_color.alphaF());
	if (background != m_background) {
		m_background = background;
		m_foreground = swatchTextColor(background);
	}
	QPainter painter(this);
	m_surface.paint(this, RoundedRole::Content, painter);
	painter.setPen(m_foreground);
	painter.drawText(this->contentsRect(), this->alignment(), this->text());
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
		auto url = urls.at(i);
		if (!url.isLocalFile() || url.toLocalFile().isEmpty())
			continue;
		ret = true;

		/* Load file icons without decoding image contents during history
		 * binding. SVG artwork is resolved lazily by the first visible paint. */
		const QFileInfo fileinfo(url.toLocalFile());
		QIcon icon = fileinfo.exists() ? Platform::fileIcon(fileinfo.absoluteFilePath()) : QIcon();
		if (icon.isNull())
			icon = QFileIconProvider().icon(QFileIconProvider::File);
		const QPixmap pixmap = icon.pixmap(256, 256);
		QLabel *label = new QLabel(this);
		if (FilePreview::isSvg(url)) {
			label->setObjectName("SvgFilePreview");
			m_svg_sources.append({int(m_labels.size()), url, pixmap});
		}
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

void FileFrame::updateSvgPreviews(void)
{
	if (m_svg_sources.isEmpty() || m_labels.isEmpty()) return;
	const int size = m_labels.first().first->width();
	if (size <= 0) return;
	const int pixels = qBound(1, qCeil(size*devicePixelRatioF()), ClipboardData::previewPixels);
	if (pixels == m_svg_preview_pixels) return;
	m_svg_preview_pixels = pixels;
	for (const auto &source : m_svg_sources) {
		const QImage image = FilePreview::loadImage(source.url, pixels);
		m_labels[source.label].second = image.isNull() ? source.icon : QPixmap::fromImage(image);
		m_last_label_size = -1;
	}
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
	this->updateSvgPreviews();
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

void StackedWidget::setPixmap(const QPixmap &pixmap, const QSize &originalSize)
{
	PixmapFrame *pixmap_frame = new PixmapFrame(this);

	pixmap_frame->setStorePixmap(pixmap);
	QString s = QString("%1x%2 ").arg(originalSize.width()).arg(originalSize.height()) + QObject::tr("px");
	pixmap_frame->setMaskFrameText(s);

	this->addWidget(pixmap_frame);
}

void StackedWidget::setText(QString &s)
{
	if (QColor(s).isValid()) {
		this->addWidget(new ColorFrame(s, this));
		return;
	}
	TextFrame *text_frame = new TextFrame(this);

	/* Cards only display a short excerpt. Keep the full value in the entry
	 * and search text, without making QLabel lay out a multi-megabyte log. */
	text_frame->setTextFormat(Qt::PlainText);
	text_frame->setText(textPreview(s));
	text_frame->setIndent(4);
	text_frame->setMaskFrameText(QString("%1 ").arg(s.size()) + QObject::tr("characters"));

	this->addWidget(text_frame);
}

void StackedWidget::setRichText(QString &richText, QString &plainText)
{
	if (QColor(plainText.trimmed()).isValid()) {
		this->addWidget(new ColorFrame(plainText, this));
		return;
	}
	TextFrame *richtext_frame = new TextFrame(this);

	/* Do not slice HTML through tags. Oversized markup uses the plain-text
	 * excerpt; ordinary rich snippets retain their original formatting. */
	const QString preview = textPreview(plainText);
	const bool excerpt = richText.size() > richPreviewLimit || preview != plainText;
	richtext_frame->setTextFormat(excerpt ? Qt::PlainText : Qt::RichText);
	richtext_frame->setText(excerpt ? preview : richText);
	richtext_frame->setMaskFrameText(QString("%1 ").arg(plainText.size()) + QObject::tr("characters"));

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
