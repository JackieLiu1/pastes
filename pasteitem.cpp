#include "pasteitem.h"
#include "roundedwidgets.h"

#include <QClipboard>
#include <QApplication>
#include <QVBoxLayout>
#include <QMimeData>
#include <QUrl>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QContextMenuEvent>
#include <QStyle>
#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>

class CardActionButton : public RoundedButton
{
public:
	CardActionButton(bool remove, QWidget *parent) : RoundedButton(parent), m_remove(remove)
	{
		setFixedSize(30, 30);
		setFocusPolicy(Qt::NoFocus);
		setAutoDefault(false);
		setObjectName(remove ? "CardDelete" : "CardPreview");
		setProperty("destructive", remove);
		setToolTip(remove ? QObject::tr("Delete from history (Delete)") : QObject::tr("Preview (Space)"));
		setAccessibleName(remove ? QObject::tr("Delete") : QObject::tr("Preview"));
	}

protected:
	void paintEvent(QPaintEvent *event) override
	{
		RoundedButton::paintEvent(event);
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.translate(width()/2.0-9, height()/2.0-9);
		QWidget *card = parentWidget()->parentWidget();
		const bool dark = qApp->property("pastesDark").toBool();
		const bool darkSurface = dark || card->property("contentKind") == "code";
		const QColor color = m_remove && underMouse() ? QColor(darkSurface ? "#F0A799" : "#B95342") :
			QColor(dark ? "#BFBFBF" : (darkSurface ? "#B6CABD" : "#647D70"));
		painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.setBrush(Qt::NoBrush);
		if (m_remove) {
			painter.drawRoundedRect(QRectF(5, 6, 8, 10), 1.5, 1.5);
			painter.drawLine(QPointF(3, 4), QPointF(15, 4));
			painter.drawLine(QPointF(7, 2), QPointF(11, 2));
			painter.drawLine(QPointF(8, 8), QPointF(8, 13));
			painter.drawLine(QPointF(10, 8), QPointF(10, 13));
		} else {
			QPainterPath eye;
			eye.moveTo(1, 9);
			eye.cubicTo(5, 2, 13, 2, 17, 9);
			eye.cubicTo(13, 16, 5, 16, 1, 9);
			painter.drawPath(eye);
			painter.drawEllipse(QPointF(9, 9), 2.5, 2.5);
		}
	}

private:
	bool m_remove;
};

PasteItem::PasteItem(QWidget *parent, QListWidgetItem *item) : QWidget(parent),
	m_frame(new RoundedWidget(RoundedRole::Card, this)),
	m_frame_effect(new QGraphicsDropShadowEffect(this)),
	m_barnner(new Barnner(this->m_frame)),
	m_context(new StackedWidget(this->m_frame)),
	m_quick_paste_number(new RoundedLabel(RoundedRole::Number, this->m_frame)),
	m_actions(new QWidget(this->m_frame)),
	m_listwidget_item(item)
{
	this->setFocusPolicy(Qt::StrongFocus);
	this->setAttribute(Qt::WA_TranslucentBackground);

	m_frame_effect->setOffset(0, 2);
	m_frame_effect->setColor(QColor(19, 44, 38, 22));
	m_frame_effect->setBlurRadius(8);
	m_frame->setGraphicsEffect(m_frame_effect);
	m_frame->setObjectName("PasteItemFrame");

	QVBoxLayout *vboxlayout = new QVBoxLayout();
	vboxlayout->addWidget(m_barnner);
	vboxlayout->addWidget(m_context);
	vboxlayout->setSpacing(0);
	/* Reserve the widest outline even when unselected: opaque content must
	 * stay inside the 2 px selection border, without moving on selection. */
	vboxlayout->setContentsMargins(2, 2, 2, 2);

	m_frame->setLayout(vboxlayout);
	m_frame->show();
	m_quick_paste_number->setObjectName("QuickPasteNumber");
	m_quick_paste_number->setAlignment(Qt::AlignCenter);
	m_quick_paste_number->hide();
	auto *preview = new CardActionButton(false, m_actions);
	auto *remove = new CardActionButton(true, m_actions);
	QHBoxLayout *actions = new QHBoxLayout(m_actions);
	actions->setContentsMargins(0, 0, 0, 0);
	actions->setSpacing(2);
	actions->addWidget(preview);
	actions->addWidget(remove);
	QObject::connect(preview, &QPushButton::clicked, this, &PasteItem::previewRequested);
	QObject::connect(remove, &QPushButton::clicked, this, &PasteItem::deleteRequested);
	m_actions->hide();
}

void PasteItem::setCardKind(const char *kind)
{
	m_frame->setProperty("contentKind", kind);
	m_frame->style()->unpolish(m_frame);
	m_frame->style()->polish(m_frame);
	/* Content children inherit their kind-specific colors from the theme. */
	for (QWidget *child : m_frame->findChildren<QWidget *>()) {
		child->style()->unpolish(child);
		child->style()->polish(child);
	}
}

void PasteItem::setSelected(bool selected)
{
	if (m_frame->property("selected").isValid() && m_frame->property("selected").toBool() == selected)
		return;
	m_frame->setProperty("selected", selected);
	m_frame->style()->unpolish(m_frame);
	m_frame->style()->polish(m_frame);
	m_frame->update();
	this->updateActions();
}

void PasteItem::updateActions(void)
{
	const bool visible = (m_hovered || m_frame->property("selected").toBool()) &&
		!m_frame->property("pressed").toBool();
	m_actions->setVisible(visible);
	m_barnner->setActionsVisible(visible);
	if (visible) m_actions->raise();
}

void PasteItem::enterEvent(QEnterEvent *event)
{
	m_hovered = true;
	this->updateActions();
	QWidget::enterEvent(event);
}

void PasteItem::leaveEvent(QEvent *event)
{
	m_hovered = false;
	this->updateActions();
	QWidget::leaveEvent(event);
}

void PasteItem::setQuickPasteNumber(int number)
{
	if (number < 1 || number > 9) {
		m_quick_paste_number->hide();
		return;
	}
	m_quick_paste_number->setText(QString::number(number));
	m_quick_paste_number->setToolTip(QObject::tr("Ctrl+%1 to paste").arg(number));
	m_quick_paste_number->show();
	m_quick_paste_number->raise();
}

void PasteItem::setPressed(bool pressed)
{
	if (m_frame->property("pressed").toBool() == pressed) return;
	m_frame->setProperty("pressed", pressed);
	m_frame->update();
	this->updateActions();
}

void PasteItem::setImage(QImage &image)
{
	QPixmap pixmap = QPixmap::fromImage(image);
	m_context->setPixmap(pixmap);
	this->m_barnner->setTitle(QObject::tr("Image"));
	this->setCardKind("image");
}

void PasteItem::setPlainText(QString s)
{
	m_context->setText(s);
	m_text = s;

	if (s.startsWith("http://") || s.startsWith("ftp://") || s.startsWith("https://")) {
		this->m_barnner->setTitle(QObject::tr("Link"));
		this->setCardKind("link");
	} else if (QColor::isValidColor(s)) {
		this->m_barnner->setTitle(QObject::tr("Color"));
		this->setCardKind("color");
	} else {
		const bool code = s.contains('\n') &&
					(s.contains('{') || s.contains("#include") || s.contains("def "));
		this->m_barnner->setTitle(code ? QObject::tr("Code") : QObject::tr("Text"));
		this->setCardKind(code ? "code" : "text");
	}
}

void PasteItem::setRichText(QString richText, QString plainText)
{
	m_context->setRichText(richText, plainText);
	m_text = plainText;

	if (plainText.startsWith("http://") || plainText.startsWith("ftp://") || plainText.startsWith("https://")) {
		this->m_barnner->setTitle(QObject::tr("Link"));
		this->setCardKind("link");
	} else if (QColor::isValidColor(plainText.simplified().trimmed())) {
		this->m_barnner->setTitle(QObject::tr("Color"));
		this->setCardKind("color");
	} else {
		this->m_barnner->setTitle(QObject::tr("Text"));
		this->setCardKind("text");
	}
}

bool PasteItem::setUrls(QList<QUrl> &urls)
{
	for (auto &url : urls) {
		m_text += url.toString();
	}
	if (!m_context->setUrls(urls))
		return false;

	this->m_barnner->setTitle(QString("%1 ").arg(urls.count()) + QObject::tr("Files"));
	this->setCardKind("file");
	return true;
}

void PasteItem::setIcon(QPixmap pixmap)
{
	m_barnner->setIcon(pixmap);
}

void PasteItem::setTime(QDateTime &dateTime)
{
	m_barnner->setTime(dateTime);
}

void PasteItem::resizeEvent(QResizeEvent *event)
{
	QSize size = event->size();
	m_frame->setGeometry(4, 4, qMax(0, size.width()-8), qMax(0, size.height()-8));
	m_barnner->setFixedHeight(44);
	m_quick_paste_number->setGeometry(m_frame->width()-34, m_frame->height()-30, 22, 22);
	m_actions->setGeometry(m_frame->width()-74, 8, 62, 30);

	QWidget::resizeEvent(event);
}

bool PasteItem::event(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
			emit this->moveFocusPrevNext(key->key() == Qt::Key_Backtab ||
						     key->modifiers().testFlag(Qt::ShiftModifier));
			return true;
		}
	}
	return QWidget::event(event);
}

void PasteItem::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);
}

void PasteItem::keyPressEvent(QKeyEvent *event)
{
	switch (event->key()) {
	case Qt::Key_Return:
	case Qt::Key_Enter:
		this->copyData(event->modifiers() & Qt::ShiftModifier);
		return;
	case Qt::Key_Escape:
		emit this->hideWindow();
		return;
	case Qt::Key_Left:
		emit this->moveFocusPrevNext(true);
		return;
	case Qt::Key_Right:
		emit this->moveFocusPrevNext(false);
		return;
	case Qt::Key_Space:
		emit this->previewRequested();
		return;
	case Qt::Key_Delete:
		emit this->deleteRequested();
		return;
	case Qt::Key_C:
		if (event->modifiers() & Qt::ControlModifier) {
			this->copyData(false, false);
			return;
		}
		break;
	}

	QWidget::keyPressEvent(event);
}

void PasteItem::contextMenuEvent(QContextMenuEvent *event)
{
	QMenu menu(this);
	QAction *pasteAction = menu.addAction(QObject::tr("Paste"));
	QAction *plainAction = menu.addAction(QObject::tr("Paste as Plain Text"));
	ItemData *data = reinterpret_cast<ItemData *>(this->m_listwidget_item->data(Qt::UserRole).value<uint64_t>());
	plainAction->setEnabled(data && data->mimeData->hasText());
	QAction *copyAction = menu.addAction(QObject::tr("Copy to Clipboard"));
	menu.addSeparator();
	QAction *previewAction = menu.addAction(QObject::tr("Preview"));
	QAction *deleteAction = menu.addAction(QObject::tr("Delete"));
	QAction *chosen = menu.exec(event->globalPos());
	if (chosen == pasteAction)
		this->copyData();
	else if (chosen == plainAction)
		this->copyData(true);
	else if (chosen == copyAction)
		this->copyData(false, false);
	else if (chosen == previewAction)
		emit this->previewRequested();
	else if (chosen == deleteAction)
		emit this->deleteRequested();
}

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/XTest.h>

#include <QTimer>
#include <QThread>

static void SendKey(Display * disp, KeySym keysym, KeySym modsym)
{
	KeyCode keycode = 0, modcode = 0;
	keycode = XKeysymToKeycode (disp, keysym);
	if (keycode == 0)
		return;

	XTestGrabControl (disp, True);
	/* Generate modkey press */
	if (modsym != 0) {
		modcode = XKeysymToKeycode(disp, modsym);
		XTestFakeKeyEvent (disp, modcode, True, 0);
	}
	/* Generate regular key press and release */
	XTestFakeKeyEvent (disp, keycode, True, 0);
	XTestFakeKeyEvent (disp, keycode, False, 0);

	/* Generate modkey release */
	if (modsym != 0)
		XTestFakeKeyEvent (disp, modcode, False, 0);

	XSync (disp, False);
	XTestGrabControl (disp, False);
}
#endif

void PasteItem::copyData(bool plainText, bool paste)
{
	ItemData *itemData = reinterpret_cast<ItemData *>(this->m_listwidget_item->data(Qt::UserRole).value<uint64_t>());
	if (!itemData || (plainText && !itemData->mimeData->hasText()))
		return;
	if (paste)
		emit this->hideWindow();

	QClipboard *clipboard = QApplication::clipboard();

	if (plainText && itemData->mimeData->hasText())
		clipboard->setText(itemData->mimeData->text(), QClipboard::Clipboard);
	else
		clipboard->setMimeData(dup_mimedata(itemData->mimeData), QClipboard::Clipboard);

#ifdef Q_OS_LINUX
	if (plainText && itemData->mimeData->hasText())
		clipboard->setText(itemData->mimeData->text(), QClipboard::Selection);
	else
		clipboard->setMimeData(dup_mimedata(itemData->mimeData), QClipboard::Selection);
#endif
	emit this->clipboardUpdated();
	if (paste)
		emit this->copied();
	if (itemData->mimeData->hasUrls())
		return;

#ifdef Q_OS_LINUX
	/* Send keypress event 'Ctrl +v' for direct paste */
	if (paste) QTimer::singleShot(1000, [](void) {
		Display *disp = XOpenDisplay(nullptr);
		SendKey(disp, XK_Insert, XK_Shift_L);
		XCloseDisplay(disp);
	});
#endif
}
