#include "ui/pasteitem.h"
#include "ui/roundedwidgets.h"
#include "core/clipboardcontent.h"
#include "platform/menuintegration.h"
#include "ui/filepreview.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QMimeData>
#include <QUrl>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QPointer>
#include <QContextMenuEvent>
#include <QStyle>
#include <QtMath>

PasteItem::PasteItem(QWidget *parent, QListWidgetItem *item) : QWidget(parent),
	m_frame(new RoundedWidget(RoundedRole::Card, this)),
	m_frame_effect(new QGraphicsDropShadowEffect(this)),
	m_barnner(new Barnner(this->m_frame)),
	m_context(new StackedWidget(this->m_frame)),
	m_quick_paste_number(new RoundedLabel(RoundedRole::Number, this->m_frame)),
	m_listwidget_item(item)
{
	/* Pointer gestures select on release, after ruling out browsing. */
	this->setFocusPolicy(Qt::TabFocus);
	this->setAttribute(Qt::WA_TranslucentBackground);

	m_frame_effect->setOffset(0, 2);
	m_frame_effect->setColor(QColor(19, 44, 38, 22));
	m_frame_effect->setBlurRadius(8);
	m_frame->setGraphicsEffect(m_frame_effect);
	m_frame->setObjectName("PasteItemFrame");
	connect(m_barnner, &Barnner::favoriteRequested, this, &PasteItem::favoriteRequested);

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
}

QPixmap PasteItem::beginSwipe(void)
{
	const qreal ratio = this->devicePixelRatioF();
	QPixmap snapshot(qCeil(this->width()*ratio), qCeil(this->height()*ratio));
	snapshot.setDevicePixelRatio(ratio);
	snapshot.fill(Qt::transparent);
	/* Render just the card, without inheriting the viewport background or
 * capturing an effect recursively. Keep the existing shadow for return. */
	m_frame_effect->setEnabled(false);
	QPainter painter(&snapshot);
	m_frame->render(&painter, m_frame->pos(), QRegion(), QWidget::DrawChildren);
	painter.end();
	m_frame_effect->setEnabled(true);
	m_frame->hide();
	return snapshot;
}

void PasteItem::endSwipe(void)
{
	m_frame->show();
}

void PasteItem::setImage(const QImage &image, const QSize &originalSize)
{
	const QImage preview = image.width() > ClipboardData::previewPixels || image.height() > ClipboardData::previewPixels ?
		image.scaled(ClipboardData::previewPixels, ClipboardData::previewPixels, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
	const QPixmap pixmap = QPixmap::fromImage(preview);
	m_context->setPixmap(pixmap, originalSize.isValid() ? originalSize : image.size());
	this->m_barnner->setTitle(QObject::tr("Image"));
	this->setCardKind("image");
}

bool PasteItem::setImage(const QMimeData *mime)
{
	QSize originalSize;
	const QImage preview = ClipboardData::previewImage(mime, &originalSize);
	if (preview.isNull()) return false;
	setImage(preview, originalSize);
	return true;
}

void PasteItem::setPlainText(QString s)
{
	if (this->setPathPreview(s))
		return;
	m_context->setText(s);
	m_text = s;

	if (s.startsWith("http://") || s.startsWith("ftp://") || s.startsWith("https://")) {
		this->m_barnner->setTitle(QObject::tr("Link"));
		this->setCardKind("link");
	} else if (QColor(s).isValid()) {
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
	if (this->setPathPreview(plainText))
		return;
	m_context->setRichText(richText, plainText);
	m_text = plainText;

	if (plainText.startsWith("http://") || plainText.startsWith("ftp://") || plainText.startsWith("https://")) {
		this->m_barnner->setTitle(QObject::tr("Link"));
		this->setCardKind("link");
	} else if (QColor(plainText.simplified().trimmed()).isValid()) {
		this->m_barnner->setTitle(QObject::tr("Color"));
		this->setCardKind("color");
	} else {
		this->m_barnner->setTitle(QObject::tr("Text"));
		this->setCardKind("text");
	}
}

bool PasteItem::setPathPreview(const QString &text)
{
	const QUrl url = FilePreview::localUrl(text);
	if (url.isEmpty())
		return false;
	QList<QUrl> urls{url};
	if (!this->setUrls(urls))
		return false;
	/* A path preview must still search and paste as the copied text. */
	this->m_text = text;
	this->m_barnner->setTitle(QObject::tr("File path"));
	return true;
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

	QWidget::resizeEvent(event);
}

bool PasteItem::event(QEvent *event)
{
	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
			emit this->moveFocusPrevNext(key->key() == Qt::Key_Backtab ||
						     key->modifiers().testFlag(Qt::ShiftModifier), true);
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
		emit this->moveFocusPrevNext(true, false);
		return;
	case Qt::Key_Right:
		emit this->moveFocusPrevNext(false, false);
		return;
	case Qt::Key_Space:
		emit this->previewRequested();
		return;
	case Qt::Key_Delete:
		emit this->deleteRequested();
		return;
	case Qt::Key_F2:
		if (m_entry && m_entry->favorite) { emit renameFavoriteRequested(); return; }
		break;
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
	/* Clipboard updates can replace this card while a menu is tracking. */
	QPointer<PasteItem> guard(this);
	QMenu menu(this->window());
	QAction *copyAction = menu.addAction(QObject::tr("Copy to Clipboard"));
	QAction *plainAction = menu.addAction(QObject::tr("Copy as Plain Text"));
	const HistoryEntry data = m_entry;
	plainAction->setEnabled(data && data->mimeData->hasText());
	menu.addSeparator();
	QAction *favoriteAction = menu.addAction(data && data->favorite ?
		QObject::tr("Remove from Favorites") : QObject::tr("Add to Favorites"));
	QAction *renameAction = nullptr, *leftAction = nullptr, *rightAction = nullptr;
	if (data && data->favorite) {
		renameAction = menu.addAction(QObject::tr("Name Favorite…"));
		renameAction->setShortcut(QKeySequence(Qt::Key_F2));
		if (m_moveLeft || m_moveRight) {
			leftAction = menu.addAction(QObject::tr("Move Left"));
			rightAction = menu.addAction(QObject::tr("Move Right"));
			leftAction->setEnabled(m_moveLeft);
			rightAction->setEnabled(m_moveRight);
		}
	}
	QAction *previewAction = menu.addAction(QObject::tr("Preview"));
	QAction *deleteAction = menu.addAction(QObject::tr("Delete"));
	QAction *chosen = Platform::execMenuAt(&menu, this->window(), event->globalPos());
	if (!guard || guard->isHidden())
		return;
	if (chosen == copyAction)
		this->copyData(false, false);
	else if (chosen == plainAction)
		this->copyData(true, false);
	else if (chosen == favoriteAction)
		emit this->favoriteRequested();
	else if (chosen && chosen == renameAction)
		emit renameFavoriteRequested();
	else if (chosen && (chosen == leftAction || chosen == rightAction))
		emit moveFavoriteRequested(chosen == leftAction);
	else if (chosen == previewAction)
		emit this->previewRequested();
	else if (chosen == deleteAction)
		emit this->deleteRequested();
}

void PasteItem::copyData(bool plainText, bool paste)
{
	if (!m_entry || (plainText && !m_entry->mimeData->hasText())) return;
	emit copyRequested(m_entry, plainText, paste);
}

void PasteItem::updateFavorite(void)
{
	m_barnner->setFavorite(m_entry && m_entry->favorite);
	const QString name = m_entry && m_entry->favorite ? m_entry->favoriteDetails.name : QString();
	m_barnner->setFavoriteName(name);
}

void PasteItem::setFavoriteMoves(bool left, bool right)
{
	m_moveLeft = left;
	m_moveRight = right;
}

bool PasteItem::setEntry(const HistoryEntry &entry, bool loaded)
{
	m_entry = entry;
	const QMimeData *mime = entry->mimeData;
	QList<QUrl> urls = mime->urls();
	bool hasContent = false;
	if (ClipboardContent::prefersImage(*mime) && setImage(mime)) {
		/* Keep the accompanying path searchable without reading its file. */
		m_text = mime->text();
		for (const QUrl &url : urls) m_text += url.toString();
		hasContent = true;
	} else if (loaded) {
		bool localFiles = !urls.isEmpty();
		for (const QUrl &url : urls) localFiles &= url.isLocalFile();
		if (localFiles && setUrls(urls)) hasContent = true;
		else if (mime->hasHtml() && !mime->text().isEmpty()) {
			setRichText(mime->html(), mime->text());
			hasContent = true;
		} else if (mime->hasImage() && setImage(mime)) hasContent = true;
		else if (mime->hasUrls()) hasContent = !localFiles && setUrls(urls);
		else if (mime->hasText() && !mime->text().isEmpty()) {
			setPlainText(mime->text().trimmed());
			hasContent = true;
		}
	} else {
		if (mime->hasUrls() && !urls.isEmpty()) setUrls(urls);
		else if (mime->hasHtml() && !mime->text().trimmed().isEmpty())
			setRichText(mime->html(), mime->text().trimmed());
		else if (mime->hasImage()) setImage(mime);
		else setPlainText(mime->text().trimmed());
		hasContent = true;
	}
	updateFavorite();
	setTime(entry->time);
	setIcon(QPixmap::fromImage(entry->icon));
	return hasContent;
}
