#include "roundedwidgets.h"

#include <QApplication>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

bool RoundedSurfaceStyle::operator==(const RoundedSurfaceStyle &other) const
{
	return fill == other.fill && border == other.border && radius == other.radius &&
		borderWidth == other.borderWidth && corners == other.corners && gradient == other.gradient;
}

static RoundedSurfaceStyle surfaceStyle(QWidget *widget, RoundedRole role)
{
	const bool dark = qApp->property("pastesDark").toBool();
	QWidget *card = widget;
	while (card && card->objectName() != "PasteItemFrame")
		card = card->parentWidget();
	const QString kind = card ? card->property("contentKind").toString() : QString();
	RoundedSurfaceStyle style;
	style.fill = Qt::transparent;
	style.border = Qt::transparent;
	switch (role) {
	case RoundedRole::Panel:
#ifdef Q_OS_MACOS
		style.fill = QColor(dark ? "#181818" : "#F4F4EF");
		if (widget->window()->property("macPanelBackdrop").toBool())
			style.fill.setAlpha(dark ? 72 : 52);
		style.border = QColor(255, 255, 255, dark ? 76 : 120);
		/* Keep the glass outline one physical pixel wide on Retina displays. */
		style.borderWidth = 1/widget->devicePixelRatioF();
		style.radius = 18; style.corners = 1;
#else
		style.fill = dark ? QColor("#181818") : QColor(244, 244, 239, 245);
		style.border = dark ? QColor(190, 190, 190, 38) : QColor(66, 88, 75, 32);
		style.radius = 18; style.borderWidth = 1; style.corners = 1;
#endif
		break;
	case RoundedRole::Card:
		style.fill = QColor(dark ? "#282828" : "#FFFDF8");
		style.border = QColor(dark ? "#414141" : "#DADFD4");
		if (kind == "link") style.fill = QColor(dark ? "#282828" : "#F5FAF5");
		if (kind == "image") style.fill = QColor(dark ? "#282828" : "#F0F5F1");
		if (kind == "code") {
			style.fill = QColor(dark ? "#202020" : "#203B34");
			style.border = QColor(dark ? "#393939" : "#315148");
		}
		style.radius = 14; style.borderWidth = 1;
		if (widget->property("selected").toBool()) {
			style.border = QColor(dark ? "#76C5AA" : "#359782");
			style.borderWidth = 2;
		}
		if (widget->property("pressed").toBool())
			style.fill = dark ? style.fill.lighter(108) : style.fill.darker(103);
		break;
	case RoundedRole::Search:
		style.fill = QColor(dark ? "#242424" : "#FCFCF8");
		style.border = QColor(dark ? "#434343" : "#D8DED2");
		if (widget->underMouse()) style.border = QColor(dark ? "#5A5A5A" : "#AEBFB0");
		if (widget->hasFocus()) {
			style.fill = QColor(dark ? "#2A2A2A" : "#FFFDF8");
			style.border = QColor(dark ? "#76C5AA" : "#359782");
		}
		style.radius = 11; style.borderWidth = 1;
		break;
	case RoundedRole::HistoryBadge:
		style.fill = QColor(dark ? "#2A2A2A" : "#E0EDE4"); style.radius = 10;
		break;
	case RoundedRole::Number:
		style.fill = dark ? QColor(42, 42, 42, 230) : QColor(255, 253, 248, 220);
		style.border = QColor(dark ? "#525252" : "#D8E0D5");
		style.radius = 6; style.borderWidth = 1;
		break;
	case RoundedRole::MenuButton:
		if (widget->property("primary").toBool()) {
			style.fill = QColor(dark ? "#76C5AA" : "#237B68");
			if (widget->underMouse()) style.fill = QColor(dark ? "#8BD3BA" : "#2A8B76");
			if (static_cast<QPushButton *>(widget)->isDown())
				style.fill = QColor(dark ? "#65B59A" : "#1C6958");
			style.radius = 10;
			break;
		}
		if (widget->underMouse()) style.fill = QColor(dark ? "#333333" : "#E3EAE0");
		if (static_cast<QPushButton *>(widget)->isDown())
			style.fill = QColor(dark ? "#3D3D3D" : "#D7E1D3");
		if (widget->property("destructive").toBool() && widget->underMouse())
			style.fill = QColor(dark ? "#563B34" : "#F6E7DF");
		if (widget->hasFocus()) {
			style.border = QColor(dark ? "#76C5AA" : "#359782");
			style.borderWidth = 1;
		}
		style.radius = 10;
		break;
	case RoundedRole::Preview:
		style.fill = QColor(dark ? "#1F1F1F" : "#F4F4EF");
		style.border = QColor(dark ? "#414141" : "#DADFD4");
		style.radius = 18; style.borderWidth = 1;
		break;
	case RoundedRole::PreviewContent:
		style.fill = QColor(dark ? "#282828" : "#FFFDF8");
		style.border = QColor(dark ? "#3D3D3D" : "#E1E5DA");
		style.radius = 12; style.borderWidth = 1;
		break;
	case RoundedRole::Content:
		if (widget->property("swatchColor").isValid()) {
			style.fill = widget->property("swatchColor").value<QColor>();
			style.radius = 12; style.corners = 2;
		}
		break;
	case RoundedRole::Footer: {
		style.radius = 12; style.corners = 2;
		QWidget *content = widget->parentWidget();
		const QColor swatch = content->property("swatchColor").value<QColor>();
		if (swatch.isValid()) {
			style.fill = qGray(swatch.rgb()) < 145 ? QColor(0, 0, 0, 20) : QColor(255, 255, 255, 35);
		} else if (content->objectName() == "ContextPixmapFrame") {
			style.fill = QColor(dark ? "#282828" : "#F0F5F1");
		} else {
			style.gradient = true;
			style.fill = QColor(dark ? "#282828" : "#FFFDF8");
			if (kind == "code") style.fill = QColor(dark ? "#202020" : "#203B34");
		}
		break;
	}
	}
	return style;
}

void RoundedSurface::paint(QWidget *widget, RoundedRole role, QPainter &painter)
{
	const RoundedSurfaceStyle style = surfaceStyle(widget, role);
	if ((style.fill.alpha() == 0 && style.border.alpha() == 0) || widget->size().isEmpty())
		return;
	const qreal ratio = widget->devicePixelRatioF();
	if (m_pixmap.isNull() || m_size != widget->size() || m_ratio != ratio || !(m_style == style)) {
		m_size = widget->size(); m_ratio = ratio; m_style = style;
		const QSize pixels(qCeil(m_size.width()*ratio), qCeil(m_size.height()*ratio));
		QImage image(pixels*2, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter render(&image);
		render.setRenderHint(QPainter::Antialiasing);
		render.scale(qreal(image.width())/m_size.width(), qreal(image.height())/m_size.height());
		const qreal inset = style.borderWidth/2;
		const QRectF rect(inset, inset, m_size.width()-2*inset, m_size.height()-2*inset);
		const qreal radius = qMax(qreal(0), qMin(style.radius-inset, qMin(rect.width(), rect.height())/2));
		QPainterPath path;
		const qreal arc = radius*0.5522847498;
		if (style.corners == 0) {
			path.addRoundedRect(rect, radius, radius);
		} else if (style.corners == 1) {
			path.moveTo(rect.left(), rect.bottom());
			path.lineTo(rect.left(), rect.top()+radius);
			path.cubicTo(rect.left(), rect.top()+radius-arc, rect.left()+radius-arc, rect.top(),
				rect.left()+radius, rect.top());
			path.lineTo(rect.right()-radius, rect.top());
			path.cubicTo(rect.right()-radius+arc, rect.top(), rect.right(), rect.top()+radius-arc,
				rect.right(), rect.top()+radius);
			path.lineTo(rect.bottomRight()); path.closeSubpath();
		} else {
			path.moveTo(rect.topLeft()); path.lineTo(rect.topRight());
			path.lineTo(rect.right(), rect.bottom()-radius);
			path.cubicTo(rect.right(), rect.bottom()-radius+arc, rect.right()-radius+arc, rect.bottom(),
				rect.right()-radius, rect.bottom());
			path.lineTo(rect.left()+radius, rect.bottom());
			path.cubicTo(rect.left()+radius-arc, rect.bottom(), rect.left(), rect.bottom()-radius+arc,
				rect.left(), rect.bottom()-radius);
			path.closeSubpath();
		}
		QBrush fill(style.fill);
		if (style.gradient) {
			QLinearGradient gradient(0, 0, 0, m_size.height());
			QColor transparent = style.fill; transparent.setAlpha(0);
			QColor middle = style.fill; middle.setAlpha(qMin(235, style.fill.alpha()));
			gradient.setColorAt(0, transparent); gradient.setColorAt(0.4, middle);
			gradient.setColorAt(1, style.fill); fill = QBrush(gradient);
		}
		render.setBrush(fill);
		render.setPen(style.borderWidth > 0 ? QPen(style.border, style.borderWidth) : QPen(Qt::NoPen));
		render.drawPath(path); render.end();
		m_pixmap = QPixmap::fromImage(image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
		m_pixmap.setDevicePixelRatio(ratio);
	}
	painter.setRenderHint(QPainter::SmoothPixmapTransform);
	painter.drawPixmap(QRectF(widget->rect()), m_pixmap, QRectF(m_pixmap.rect()));
}

RoundedWidget::RoundedWidget(RoundedRole role, QWidget *parent) : QWidget(parent), m_role(role) {}
void RoundedWidget::paintEvent(QPaintEvent *)
{
	QPainter painter(this); m_surface.paint(this, m_role, painter);
}

RoundedLabel::RoundedLabel(RoundedRole role, QWidget *parent) : QLabel(parent), m_role(role) {}
RoundedLabel::RoundedLabel(const QString &text, RoundedRole role, QWidget *parent) : QLabel(text, parent), m_role(role) {}
void RoundedLabel::paintEvent(QPaintEvent *event)
{
	{ QPainter painter(this); m_surface.paint(this, m_role, painter); }
	QLabel::paintEvent(event);
}

RoundedButton::RoundedButton(QWidget *parent) : QPushButton(parent) {}
void RoundedButton::paintEvent(QPaintEvent *event)
{
	{ QPainter painter(this); m_surface.paint(this, RoundedRole::MenuButton, painter); }
	QPushButton::paintEvent(event);
}
