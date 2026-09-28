#include <QApplication>
#include <QScreen>
#include <QLabel>
#include <QSizePolicy>
#include <QScrollBar>
#include <QAbstractButton>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyleHints>
#include <QFile>
#include <QMimeData>
#include <QCryptographicHash>
#include <QImage>
#include <QUrl>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QEvent>
#include <QDebug>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPointer>
#include <QHash>
#include <QScopedValueRollback>

#include "mainwindow.h"
#include "historypolicy.h"
#include "platform/clipboardsource.h"
#include "platform/pastetarget.h"
#include "pasteitem.h"
#include "previewdialog.h"
#include "appdialog.h"
#include "settingsdialog.h"
#include "cardswipe.h"
#include "cardreflow.h"
#include "elasticscroll.h"
#include "platform/windowintegration.h"
#include "platform/menuintegration.h"

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  __main_frame(new MainFrame(this)),
	  __main_frame_shadow(new QGraphicsDropShadowEffect(this)),
	  __hide_animation(new QPropertyAnimation(this, "pos")),
	  __shortcut(new GlobalShortcut(this)),
	  __hide_state(true),
	  __current_item(nullptr)
{
	const QRect geometry = Platform::panelGeometry(QApplication::primaryScreen());
	this->__recording_enabled = !QSettings().value("pauseRecording", false).toBool();

	/* The shelf spans its screen and must not expose resize handles. */
	this->setFixedSize(geometry.size());
	this->setGeometry(geometry);
	Platform::initializePanel(this);
	this->setFocusPolicy(Qt::NoFocus);
	this->setFont(QFont(QStringLiteral("Segoe UI"), 10));
	this->applyTheme(QSettings().value("theme", "light").toString());
	this->setCentralWidget(this->__main_frame);
	this->setAttribute(Qt::WA_TranslucentBackground, true);
	Platform::enablePanelBlur(this);

	this->__main_frame->setGeometry(this->geometry());
	this->__main_frame_shadow->setOffset(0, 0);
	this->__main_frame_shadow->setColor(QColor(0, 0, 0, 130));
	this->__main_frame_shadow->setBlurRadius(24);
	if (Platform::panelAppearance().shadow)
		this->__main_frame->setGraphicsEffect(this->__main_frame_shadow);
	/* Do not move focus before the pointer gesture decides it is a click. */
	this->__main_frame->setFocusPolicy(Qt::TabFocus);
	QObject::connect(this->__main_frame, &MainFrame::moveFocusPrevNext,
			 this, &MainWindow::move_to_prev_next_focus_widget);
	QObject::connect(this->__main_frame, &MainFrame::hideWindow, [this](void) {
		this->hide_window();
	});
	QObject::connect(this->__main_frame, &MainFrame::selectItem, [this](void) {
		PasteItem *widget = this->currentPasteItem();
		if (!widget)
			return;
		this->__current_item = nullptr;
		widget->copyData();
	});
	QObject::connect(this->__main_frame, &MainFrame::selectPlainTextItem, this, [this](void) {
		PasteItem *widget = this->currentPasteItem();
		if (widget)
			widget->copyData(true);
	});

	this->__clipboard_timer = new QTimer(this);
	this->__clipboard_timer->setSingleShot(true);
	this->__clipboard_source = new ClipboardSource(this);
	this->__clipboard_timer->setInterval(this->__clipboard_source->settleInterval());
	QObject::connect(this->__clipboard_timer, &QTimer::timeout, this, &MainWindow::clipboard_later);
	QObject::connect(this->__clipboard_source, &ClipboardSource::iconReady, this,
		[this](quint64 request, const QImage &icon) {
		if (request == this->__source_request)
			this->__source_icon = icon;
		if (icon.isNull())
			return;
		/* A slow icon lookup may finish after the history entry was saved.
		 * Match a live widget by request ID, never a worker-owned pointer. */
		for (int i = 0; i < this->__scroll_widget->count(); ++i) {
			QListWidgetItem *item = this->__scroll_widget->item(i);
			auto *widget = qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
			if (!widget || widget->property("sourceRequest").toULongLong() != request)
				continue;
			auto *data = reinterpret_cast<ItemData *>(item->data(Qt::UserRole).value<uint64_t>());
			if (data) {
				data->icon = icon;
				widget->setIcon(QPixmap::fromImage(icon));
				this->__db.updatePasteItemIcon(data->md5, icon);
			}
			break;
		}
	}, Qt::QueuedConnection);
	QObject::connect(&this->__db, &Database::imageEncoded, this,
		[this](quint64 request, const QByteArray &encoded, int format, qreal ratio) {
		for (int i = 0; i < this->__scroll_widget->count(); ++i) {
			QListWidgetItem *item = this->__scroll_widget->item(i);
			auto *widget = qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
			if (!widget || widget->property("imageRequest").toULongLong() != request) continue;
			auto *data = reinterpret_cast<ItemData *>(item->data(Qt::UserRole).value<uint64_t>());
			if (!data || !ClipboardData::storedImage(data->mimeData).isEmpty()) return;
			QMimeData *mime = ClipboardData::withStoredImage(data->mimeData, encoded, format, ratio);
			delete data->mimeData;
			data->mimeData = mime;
			return;
		}
	});
	auto clipboardChanged = [this](void) {
		if (!this->__recording_enabled)
			return;
		const QVariant copiedIcon = QApplication::clipboard()->mimeData()->property("pastesSourceIcon");
		++this->__source_request;
		this->__source_icon = copiedIcon.value<QImage>();
		if (!copiedIcon.isValid())
			this->__clipboard_source->capture(this->__source_request);
		/* Defer the snapshot until this change notification has returned. */
		this->__clipboard_timer->start();
	};
	QObject::connect(this->__clipboard_source, &ClipboardSource::clipboardChanged, this, clipboardChanged);
	QObject::connect(this->__hide_animation, &QPropertyAnimation::finished, [this](void) {
		if (this->__hide_animation->direction() == QAbstractAnimation::Forward) {
			/* Hidden stage */
			this->hide();
		}
	});
	this->__hide_animation->setDuration(200);
	this->__hide_animation->setStartValue(this->pos());
	this->__hide_animation->setEndValue(QPoint(geometry.x(), geometry.bottom()+1));
	this->__hide_animation->setEasingCurve(QEasingCurve::OutQuad);

	QObject::connect(this->__shortcut, &GlobalShortcut::pasteActivated, [this](void) {
		if (!this->__hide_state)
			this->hide_window();
		else
			this->show_window();
	});
	this->__primary_shortcut = this->__shortcut->primaryShortcut();
	QObject::connect(this->__shortcut, &GlobalShortcut::primaryShortcutChanged, this,
		[this](const QString &shortcut) {
		this->__primary_shortcut = shortcut;
		this->updateShortcutHint();
	});
	QApplication::instance()->installEventFilter(this);

	QShortcut *shortcut_search = new QShortcut(this);
	shortcut_search->setKey(QKeySequence("Ctrl+f"));
	QObject::connect(shortcut_search, &QShortcut::activated, [this](void) {
		LineEdit *lineedit = this->__searchbar->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly);
		lineedit->setFocus();
	});
	for (int number = 1; number <= 9; ++number) {
		QShortcut *quickPaste = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(number)), this);
		QObject::connect(quickPaste, &QShortcut::activated, this, [this, number](void) {
			this->pasteNumberedItem(number, false);
		});
		QShortcut *plainPaste = new QShortcut(QKeySequence(QString("Ctrl+Shift+%1").arg(number)), this);
		QObject::connect(plainPaste, &QShortcut::activated, this, [this, number](void) {
			this->pasteNumberedItem(number, true);
		});
	}
	QShortcut *plainSelected = new QShortcut(QKeySequence("Shift+Return"), this);
	QObject::connect(plainSelected, &QShortcut::activated, this, [this](void) {
		PasteItem *item = this->currentPasteItem();
		if (item)
			item->copyData(true);
	});

	this->initUI();
	this->__paste_target = new PasteTarget(this);
	/* The caller may still hold the copied item until its signal returns. */
	QObject::connect(this->__paste_target, &PasteTarget::permissionRequired, this, [this](void) {
		PastePermissionDialog dialog(this);
		this->execAppDialog(dialog);
	}, Qt::QueuedConnection);
	Platform::watchPanelDismissal(this, [this](bool immediate) {
		if (immediate) {
			this->__paste_target->cancel();
			this->__hide_animation->stop();
			this->__hide_state = true;
			this->hide();
		} else {
			this->hide_window();
		}
	});
}

bool MainWindow::event(QEvent *e)
{
	if (e->type() == QEvent::ActivationChange) {
		/* Qt briefly has no active window while an owned preview takes key
		 * focus. Inspect ownership after the activation handoff finishes. */
		QTimer::singleShot(0, this, [this](void) {
			if (!Platform::isOwnedWindow(this, QApplication::activeWindow()))
				this->hide_window();
		});
	}

	return QMainWindow::event(e);
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
	if (this->__scroll_widget && object == this->__scroll_widget->viewport() &&
	    event->type() == QEvent::UngrabMouse && this->__mouse_down) {
		this->resetPointerGesture();
		this->__last_clicked_item.clear();
	}
	if (this->__scroll_widget && this->isVisible() && this->handlePointerEvent(object, event))
		return true;
	if (this->__scroll_widget && object == this->__scroll_widget->viewport() &&
	    event->type() == QEvent::Resize && this->__empty_state)
		this->__empty_state->setGeometry(QRect(QPoint(0, 0), static_cast<QResizeEvent *>(event)->size()));
	if (event->type() == QEvent::KeyPress && this->isVisible() &&
	    QApplication::activeWindow() == this) {
		LineEdit *lineedit = this->__searchbar->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly);
		if (object != lineedit) {
			QKeyEvent *key = static_cast<QKeyEvent *>(event);
			if (key->key() == Qt::Key_Space)
				return QMainWindow::eventFilter(object, event);
			if (key->key() == Qt::Key_Backspace && !lineedit->text().isEmpty()) {
				lineedit->setFocus();
				lineedit->backspace();
				return true;
			}
			if (!(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
			    !key->text().isEmpty() && key->text().at(0).isPrint()) {
				lineedit->setFocus();
				lineedit->insert(key->text());
				return true;
			}
		}
	}
	return QMainWindow::eventFilter(object, event);
}

void MainWindow::resetPointerGesture(bool cancelSwipe, bool cancelReflow, bool cancelScroll)
{
	if (this->__pressed_item)
		this->__pressed_item->setPressed(false);
	this->__pressed_item.clear();
	this->__mouse_down = false;
	this->__mouse_moved = false;
	this->__pointer_gesture = PointerGesture::Pending;
	if (cancelSwipe && this->__card_swipe) this->__card_swipe->cancel();
	if (cancelReflow && this->__card_reflow) this->__card_reflow->cancel();
	if (cancelScroll && this->__elastic_scroll) this->__elastic_scroll->cancel();
	if (QWidget::mouseGrabber() == this->__scroll_widget->viewport())
		this->__scroll_widget->viewport()->releaseMouse();
	this->__scroll_widget->viewport()->unsetCursor();
}

bool MainWindow::handlePointerEvent(QObject *object, QEvent *event)
{
	const QEvent::Type type = event->type();
	if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
	    type != QEvent::MouseButtonDblClick && type != QEvent::MouseMove &&
	    type != QEvent::Wheel && type != QEvent::ContextMenu)
		return false;
	QWidget *target = qobject_cast<QWidget *>(object);
	QWidget *viewport = this->__scroll_widget->viewport();
	/* Owned menus and preview windows also have a card ancestor, but their
	 * input belongs to their own window. Only browse within the list. */
	if (!target || target->window() != this || !(target == this->__scroll_widget ||
	    target == viewport || viewport->isAncestorOf(target)))
		return false;
	PasteItem *card = nullptr;
	bool button = false;
	for (QWidget *widget = target; widget && widget != viewport; widget = widget->parentWidget()) {
		button |= qobject_cast<QAbstractButton *>(widget) != nullptr;
		if ((card = qobject_cast<PasteItem *>(widget))) break;
	}
	if (type == QEvent::Wheel) {
		this->resetPointerGesture(true, true, false);
		this->__last_clicked_item.clear();
		QWheelEvent *wheel = static_cast<QWheelEvent *>(event);
		const QPoint pixels = wheel->pixelDelta();
		const QPoint angle = wheel->angleDelta();
		const int amount = !pixels.isNull() ? (pixels.x() ? pixels.x() : pixels.y()) :
			(angle.x() ? angle.x() : angle.y())*48*qApp->styleHints()->wheelScrollLines()/120;
		this->__elastic_scroll->wheel(-amount, !pixels.isNull() || wheel->phase() != Qt::NoScrollPhase,
			wheel->phase());
		wheel->accept();
		return true;
	}
	if (type == QEvent::ContextMenu) {
		this->resetPointerGesture();
		if (card) {
			this->__scroll_widget->setCurrentItem(card->widgetItem());
			card->setFocus(Qt::MouseFocusReason);
		}
		return false;
	}
	QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
	if (button && !this->__mouse_down) return false;
	if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
		if (mouse->button() != Qt::LeftButton) {
			this->resetPointerGesture();
			if (card && mouse->button() == Qt::RightButton) {
				this->__scroll_widget->setCurrentItem(card->widgetItem());
				card->setFocus(Qt::MouseFocusReason);
			}
			return false;
		}
		this->resetPointerGesture(true, true, false);
		if (type == QEvent::MouseButtonDblClick && card && this->__last_clicked_item == card &&
		    this->__last_click_time.isValid() && this->__last_click_time.elapsed() <= QApplication::doubleClickInterval()) {
			this->__last_clicked_item.clear();
			card->copyData(mouse->modifiers().testFlag(Qt::ShiftModifier));
			return true;
		}
		this->__mouse_down = true;
		this->__mouse_press = mouse->globalPosition().toPoint();
		this->__elastic_scroll->beginDrag();
		this->__pressed_item = card;
		if (card) card->setPressed(true);
		return true;
	}
	if (type == QEvent::MouseMove) {
		/* A wheel or hide can cancel our press while Qt still reports a
		 * held button. Never let its default drag-selection handle an
		 * unowned move. Browsing and hovering must not select a card. */
		if (!this->__mouse_down) {
			mouse->accept();
			return true;
		}
		const QPoint delta = mouse->globalPosition().toPoint()-this->__mouse_press;
		if (!(mouse->buttons() & Qt::LeftButton)) {
			this->resetPointerGesture();
			this->__last_clicked_item.clear();
			return true;
		}
		if (delta.manhattanLength() >= QApplication::startDragDistance()) {
			this->__mouse_moved = true;
			this->__last_clicked_item.clear();
			if (this->__pressed_item) this->__pressed_item->setPressed(false);
		}
		/* Lock direction once the initial movement is clear. A horizontal
		 * browse cannot turn into deletion when the pointer later moves up. */
		if (this->__pointer_gesture == PointerGesture::Pending && this->__mouse_moved) {
			if (qAbs(delta.x()) > qAbs(delta.y())*1.2) {
				this->__pointer_gesture = PointerGesture::Browse;
				viewport->grabMouse(Qt::ClosedHandCursor);
			} else if (-delta.y() > qAbs(delta.x())*1.2 && this->__pressed_item) {
				this->__elastic_scroll->cancel();
				this->__scroll_widget->setCurrentItem(this->__pressed_item->widgetItem());
				this->__pressed_item->setFocus(Qt::MouseFocusReason);
				if (this->__card_swipe->begin(this->__pressed_item)) {
					this->__pointer_gesture = PointerGesture::Dismiss;
					viewport->grabMouse(Qt::ClosedHandCursor);
				}
			} else if (delta.y() > qAbs(delta.x())*1.2) {
				this->__pointer_gesture = PointerGesture::Cancelled;
			}
		}
		if (this->__pointer_gesture == PointerGesture::Browse)
			this->__elastic_scroll->dragTo(delta.x());
		else if (this->__pointer_gesture == PointerGesture::Dismiss)
			this->__card_swipe->setOffset(-delta.y());
		return true;
	}
	if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
		QPointer<PasteItem> pressed = this->__pressed_item;
		if (this->__mouse_down && this->__pointer_gesture == PointerGesture::Browse) {
			this->__elastic_scroll->dragTo(mouse->globalPosition().x()-this->__mouse_press.x());
			this->resetPointerGesture(true, true, false);
			this->__elastic_scroll->releaseDrag();
			this->__last_clicked_item.clear();
			return true;
		}
		if (this->__mouse_down && this->__pointer_gesture == PointerGesture::Dismiss && pressed) {
			/* The release position wins even if its final movement produced no
			 * MouseMove. Dragging back below the threshold always cancels. */
			this->__card_swipe->setOffset(this->__mouse_press.y()-mouse->globalPosition().y());
			const bool remove = this->__card_swipe->ready();
			this->resetPointerGesture(false);
			this->__last_clicked_item.clear();
			this->__card_swipe->release(remove);
			if (remove) {
				this->__scroll_widget->setCurrentItem(pressed->widgetItem());
				this->deleteCurrentItem();
			}
			return true;
		}
		const bool click = this->__mouse_down && !this->__mouse_moved && pressed &&
			(mouse->globalPosition().toPoint()-this->__mouse_press).manhattanLength() < QApplication::startDragDistance() &&
			pressed->rect().contains(pressed->mapFromGlobal(mouse->globalPosition().toPoint()));
		this->resetPointerGesture(true, true, false);
		this->__elastic_scroll->releaseDrag(false);
		if (click) {
			this->__scroll_widget->setCurrentItem(pressed->widgetItem());
			pressed->setFocus(Qt::MouseFocusReason);
			this->__last_clicked_item = pressed;
			this->__last_click_time.start();
		} else {
			this->__last_clicked_item.clear();
		}
		return true;
	}
	return false;
}

void MainWindow::showEvent(QShowEvent *event)
{
	QListWidgetItem *item = this->__scroll_widget->currentItem();
	if(item) {
		auto *widget = this->__scroll_widget->itemWidget(item);
		/* Let the selected item has focus */
		widget->setFocus();
		this->__scroll_widget->scrollToItem(item);
	}

	QWidget::showEvent(event);
}

void MainWindow::hideEvent(QHideEvent *event)
{
	this->resetPointerGesture();
	this->__last_clicked_item.clear();
	QWidget::hideEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	Platform::updatePanelBackdrop(this);
	if (!this->__scroll_widget)
		return;
	this->resetPointerGesture();
	const QSize size = this->cardSize();
	for (int i = 0; i < this->__scroll_widget->count(); ++i)
		this->__scroll_widget->item(i)->setSizeHint(size);
	if (this->__empty_state)
		this->__empty_state->setGeometry(this->__scroll_widget->viewport()->rect());
}

void MainWindow::show_window(void)
{
	/* Re-activating an owned dialog must not summon the history panel. */
	if (this->__app_dialog_open && Platform::panelAppearance().hideForDialog)
		return;
	if (this->__hide_state)
		this->__paste_target->captureTarget(this);
	/* A copy immediately followed by the hotkey may precede the next poll.
	 * Populate the panel before showing it, keeping the native source. */
	this->__clipboard_source->synchronize();
	if (this->__clipboard_source->settleInterval() == 0 &&
		this->__clipboard_timer->isActive()) {
		this->__clipboard_timer->stop();
		this->clipboard_later();
	}
	const QRect geometry = Platform::panelGeometry();
	this->setFixedSize(geometry.size());
	this->setGeometry(geometry);
	Platform::preparePanel(this);
	this->__hide_animation->setStartValue(this->pos());
	this->__hide_animation->setEndValue(QPoint(geometry.x(), geometry.bottom()+1));
	this->__hide_animation->setDirection(QAbstractAnimation::Backward);
	this->__hide_animation->start();
	this->__hide_state = false;
	this->show();
	Platform::activatePanel(this);
}

void MainWindow::hide_window(void)
{
	if (this->__scroll_widget) this->resetPointerGesture();
	if (this->__hide_state)
		return;

	this->__hide_animation->setDirection(QAbstractAnimation::Forward);
	this->__hide_animation->setStartValue(this->pos());
	this->__hide_animation->setEndValue(QPoint(this->x(), this->y()+this->height()));
	this->__hide_animation->start();
	this->__hide_state = true;
}

void MainWindow::pasteToPreviousWindow(bool hasUrls)
{
	this->__paste_target->paste(this, hasUrls);
}

void MainWindow::move_to_prev_next_focus_widget(bool prev, bool wrap)
{
	this->__elastic_scroll->cancel();
	if (this->__card_swipe->sourceCard()) this->__card_swipe->cancel();
	this->__card_reflow->cancel();
	const int count = this->__scroll_widget->count();
	if (count == 0)
		return;

	int row = this->__scroll_widget->currentRow();
	if (row < 0)
		row = prev ? count : -1;
	const bool fromSearch = this->__searchbar->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly)->hasFocus();

	/* Bounded by the item count: if every item is hidden (search filtered
	 * everything out) this gives up instead of looping forever. */
	PasteItem *widget = nullptr;
	for (int i = 0; i < count; i++) {
		if (prev)
			--row;
		else if (i > 0 || !fromSearch || row < 0)
			++row;
		if (row < 0 || row >= count) {
			/* Arrow navigation stops at the edge; Tab may wrap around. */
			if (!wrap)
				return;
			row = prev ? count - 1 : 0;
		}

		QListWidgetItem *item = this->__scroll_widget->item(row);
		widget = reinterpret_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
		if (widget && !item->isHidden())
			break;
	}

	if (widget && !this->__scroll_widget->item(row)->isHidden()) {
		this->__scroll_widget->setCurrentRow(row);
		this->__scroll_widget->scrollToItem(this->__scroll_widget->item(row));
		widget->setFocus();
	}
}

PasteItem *MainWindow::currentPasteItem(void)
{
	QListWidgetItem *item = this->__scroll_widget->currentItem();
	if (!item || item->isHidden())
		return nullptr;

	return reinterpret_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
}

void MainWindow::updateQuickPasteNumbers(void)
{
	int number = 0;
	for (int i = 0; i < this->__scroll_widget->count(); ++i) {
		QListWidgetItem *item = this->__scroll_widget->item(i);
		PasteItem *widget = reinterpret_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
		if (!widget)
			continue;
		widget->setQuickPasteNumber(item->isHidden() ? 0 : ++number);
	}
	if (this->__history_count) {
		const int count = this->__scroll_widget->count();
		this->__history_count->setText(number == count ? QObject::tr("%1 items").arg(count) :
					     QObject::tr("%1 of %2 items").arg(number).arg(count));
	}
	if (this->__empty_state) {
		this->__empty_state->setText(this->__scroll_widget->count() == 0 ?
			QObject::tr("Copy something to get started") : QObject::tr("No matching items"));
		this->__empty_state->setGeometry(this->__scroll_widget->viewport()->rect());
		this->__empty_state->setVisible(number == 0);
	}
}

void MainWindow::pasteNumberedItem(int number, bool plainText)
{
	int visible = 0;
	for (int i = 0; i < this->__scroll_widget->count(); ++i) {
		QListWidgetItem *item = this->__scroll_widget->item(i);
		if (!item->isHidden() && ++visible == number) {
			this->__scroll_widget->setCurrentItem(item);
			PasteItem *widget = reinterpret_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
			if (widget)
				widget->copyData(plainText);
			return;
		}
	}
}

void MainWindow::previewCurrentItem(void)
{
	this->resetPointerGesture();
	QListWidgetItem *item = this->__scroll_widget->currentItem();
	if (!item)
		return;
	ItemData *data = reinterpret_cast<ItemData *>(item->data(Qt::UserRole).value<uint64_t>());
	if (!data)
		return;
	/* Copying promotes the item and replaces its widget. Keep an owned
	 * payload for the whole preview, independent of that history update. */
	std::unique_ptr<QMimeData> mime(dup_mimedata(data->mimeData));
	const ItemData snapshot{mime.get(), data->icon, data->md5, data->time};
	PreviewDialog dialog(snapshot, this);
	auto copy = [this, &snapshot](bool plainText) {
		copyItemDataToClipboard(snapshot, plainText);
		if (this->__recording_enabled)
			this->__clipboard_timer->start();
	};
	QObject::connect(&dialog, &PreviewDialog::copyRequested, this, [copy](void) {
		copy(false);
	});
	if (dialog.exec() == QDialog::Accepted &&
		(!dialog.plainText() || snapshot.mimeData->hasText())) {
		this->hide_window();
		copy(dialog.plainText());
		this->pasteToPreviousWindow(snapshot.mimeData->hasUrls());
	} else if (this->isVisible()) {
		Platform::activatePanel(this);
		for (int i = 0; i < this->__scroll_widget->count(); ++i) {
			QListWidgetItem *candidate = this->__scroll_widget->item(i);
			auto *current = reinterpret_cast<ItemData *>(candidate->data(Qt::UserRole).value<uint64_t>());
			if (current && current->md5 == snapshot.md5) {
				this->__scroll_widget->setCurrentItem(candidate);
				if (PasteItem *widget = this->currentPasteItem()) widget->setFocus();
				break;
			}
		}
	}
}

void MainWindow::deleteCurrentItem(void)
{
	this->__elastic_scroll->cancel();
	QListWidgetItem *item = this->__scroll_widget->currentItem();
	if (!item)
		return;
	ItemData *data = reinterpret_cast<ItemData *>(item->data(Qt::UserRole).value<uint64_t>());
	if (!data)
		return;
	const int row = this->__scroll_widget->row(item);
	/* Undo owns its clone; the database worker frees the original below. */
	DeletedEntry removed;
	removed.mime.reset(dup_mimedata(data->mimeData));
	removed.icon = data->icon;
	removed.md5 = data->md5;
	removed.time = data->time;
	removed.row = row;
	removed.neighbors.reserve(this->__scroll_widget->count());
	for (int i = 0; i < this->__scroll_widget->count(); ++i) {
		QListWidgetItem *neighbor = this->__scroll_widget->item(i);
		auto *data = reinterpret_cast<ItemData *>(neighbor->data(Qt::UserRole).value<uint64_t>());
		removed.neighbors.push_back(data ? DeletedEntry::Neighbor{data->md5, data->time}
			: DeletedEntry::Neighbor{});
	}
	removed.dismissalId = this->__card_swipe->dismissalId(
		qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(item)));
	if (this->__deleted_items.size() == 20)
		this->__deleted_items.erase(this->__deleted_items.begin());
	this->__deleted_items.push_back(std::move(removed));
	this->__undo_timer->start(8000);
	if (this->__current_item == item)
		this->__current_item = nullptr;
	item->setData(Qt::UserRole, QVariant());
	QWidget *widget = this->__scroll_widget->itemWidget(item);
	if (this->__pressed_item == widget || this->__card_swipe->sourceCard() == widget)
		this->resetPointerGesture();
	this->__card_reflow->prepare(this->__scroll_widget, widget);
	this->__scroll_widget->removeItemWidget(item);
	delete item;
	if (widget) {
		widget->hide();
		widget->deleteLater();
	}
	this->__db.deletePasteItem(data);
	int next = -1;
	for (int i = row; i < this->__scroll_widget->count(); ++i) {
		if (!this->__scroll_widget->item(i)->isHidden()) { next = i; break; }
	}
	for (int i = qMin(row-1, this->__scroll_widget->count()-1); next < 0 && i >= 0; --i) {
		if (!this->__scroll_widget->item(i)->isHidden()) { next = i; break; }
	}
	this->__scroll_widget->setCurrentRow(next);
	if (next >= 0) this->__scroll_widget->itemWidget(this->__scroll_widget->item(next))->setFocus();
	else this->__main_frame->setFocus();
	this->resetItemTabOrder();
	this->updateQuickPasteNumbers();
	this->updateTrayTooltip();
	this->updateUndoState();
	this->__main_frame->layout()->activate();
	this->__card_reflow->animate();
}

void MainWindow::updateUndoState(void)
{
	const bool available = !this->__deleted_items.empty();
	this->__undo_hint->setVisible(available);
	this->__undo_button->setVisible(available);
	this->__undo_shortcut->setEnabled(available);
	if (!available) this->__undo_timer->stop();
}

void MainWindow::undoDeletion(void)
{
	if (this->__deleted_items.empty()) return;
	this->resetPointerGesture(false, false);
	/* Finish an older return before recording neighbors, but keep the
	 * outgoing dismissal alive so immediate undo can reverse its phase. */
	if (this->__card_swipe->sourceCard()) this->__card_swipe->cancel();
	DeletedEntry removed = std::move(this->__deleted_items.back());
	this->__deleted_items.pop_back();
	PasteItem *restored = nullptr;
	bool inserted = false;
	/* A fresh copy wins over the older snapshot if that content exists again. */
	for (int i = 0; i < this->__scroll_widget->count(); ++i) {
		QListWidgetItem *item = this->__scroll_widget->item(i);
		auto *data = reinterpret_cast<ItemData *>(item->data(Qt::UserRole).value<uint64_t>());
		if (data && data->md5 == removed.md5) {
			restored = qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
			break;
		}
	}
	if (!restored) {
		this->__card_reflow->prepare(this->__scroll_widget, nullptr);
		auto *data = new ItemData;
		data->mimeData = removed.mime.release();
		data->icon = removed.icon;
		data->md5 = removed.md5;
		data->time = removed.time;
		const int count = this->__scroll_widget->count();
		const int originalCount = static_cast<int>(removed.neighbors.size());
		int row = qBound(0, removed.row + count - (originalCount-1), count);
		QHash<QByteArray, int> neighborRows;
		/* Restore the original slot even when the loaded history is not sorted.
		 * Match timestamps too: a re-copied neighbor has moved to a new slot. */
		for (int i = 0; i < count; ++i) {
			auto *neighbor = reinterpret_cast<ItemData *>(this->__scroll_widget->item(i)->data(Qt::UserRole).value<uint64_t>());
			if (neighbor) neighborRows.insert(neighbor->md5, i);
		}
		auto originalNeighborRow = [this, &removed, &neighborRows, originalCount](int index) {
			if (index < 0 || index >= originalCount) return -1;
			const DeletedEntry::Neighbor &saved = removed.neighbors[index];
			const int row = neighborRows.value(saved.md5, -1);
			if (row < 0) return -1;
			auto *data = reinterpret_cast<ItemData *>(this->__scroll_widget->item(row)->data(Qt::UserRole).value<uint64_t>());
			return data->time == saved.time ? row : -1;
		};
		for (int distance = 1; distance < originalCount; ++distance) {
			const int previous = originalNeighborRow(removed.row-distance);
			if (previous >= 0) { row = previous+1; break; }
			const int next = originalNeighborRow(removed.row+distance);
			if (next >= 0) { row = next; break; }
		}
		restored = this->insertItemWidget(true, row);
		inserted = true;
		const QMimeData *mime = data->mimeData;
		if (mime->hasUrls()) {
			QList<QUrl> urls = mime->urls();
			restored->setUrls(urls);
		} else if (mime->hasHtml() && !mime->text().trimmed().isEmpty()) {
			restored->setRichText(mime->html(), mime->text().trimmed());
		} else if (mime->hasImage()) {
			restored->setImage(mime);
		} else {
			restored->setPlainText(mime->text().trimmed());
		}
		restored->setTime(data->time);
		restored->setIcon(QPixmap::fromImage(data->icon));
		restored->widgetItem()->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<uint64_t>(data)));
		restored->setProperty("imageRequest", QVariant::fromValue(this->__db.insertPasteItem(data)));
		LineEdit *search = this->__searchbar->findChild<LineEdit *>();
		restored->widgetItem()->setHidden(!restored->text().contains(search->text(), Qt::CaseInsensitive));
	}
	if (restored && !restored->widgetItem()->isHidden()) {
		this->__scroll_widget->setCurrentItem(restored->widgetItem());
		this->__scroll_widget->scrollToItem(restored->widgetItem());
		restored->setFocus();
	}
	this->resetItemTabOrder();
	this->updateQuickPasteNumbers();
	this->updateTrayTooltip();
	this->updateUndoState();
	this->__main_frame->layout()->activate();
	if (inserted) {
		this->__scroll_widget->doItemsLayout();
		this->__card_reflow->animate();
		if (!restored->widgetItem()->isHidden() && this->isVisible())
			this->__card_swipe->restore(restored, removed.dismissalId);
		else this->__card_swipe->cancel();
	} else {
		/* Re-copying already restored the content; do not replay or duplicate it. */
		this->__card_reflow->cancel();
		this->__card_swipe->cancel();
	}
}

void MainWindow::initUI(void)
{
	this->__searchbar = new SearchBar(this->__main_frame,
					  qBound(260, this->width()/3, 360), 38);
	QObject::connect(this->__searchbar, &SearchBar::hideWindow, [this](void) {
		this->hide_window();
	});
	QObject::connect(this->__searchbar, &SearchBar::textChanged, [this](const QString &text) {
		this->resetPointerGesture();
		LineEdit *lineedit = this->__searchbar->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly);
		const bool keepSearchFocus = lineedit->hasFocus();
		int temp_current_item_row = -1;
		int show_row_count = 0;

		/* Store current row num when first time searching */
		if (this->__current_item == nullptr) {
			this->__current_item = this->__scroll_widget->currentItem();
		}

		for (int i = 0; i < this->__scroll_widget->count(); i++) {
			QListWidgetItem *item = this->__scroll_widget->item(i);
			PasteItem *widget = reinterpret_cast<PasteItem *>(this->__scroll_widget->itemWidget(item));
			if (!widget->text().toLower().contains(text.toLower())) {
				item->setHidden(true);
			} else {
				item->setHidden(false);
				show_row_count++;

				if (temp_current_item_row == -1) {
					temp_current_item_row = i;
				}
			}
		}

		/* That is the first showing item */
		if (temp_current_item_row != -1) {
			this->__scroll_widget->setCurrentRow(temp_current_item_row);
			this->__scroll_widget->scrollToItem(this->__scroll_widget->item(temp_current_item_row));
		}

		if (show_row_count == this->__scroll_widget->count()) {
			/* restore current row in search before. The stored item may
			 * already have been removed by the dedup logic meanwhile. */
			if (this->__current_item) {
				this->__scroll_widget->setCurrentItem(this->__current_item);
				this->__scroll_widget->scrollToItem(this->__current_item);
			}
			this->__current_item = nullptr;
		}
		this->updateQuickPasteNumbers();
		/* Updating the list's current index can focus its item widget. Keep
		 * typing in search until the user explicitly navigates to a card. */
		if (keepSearchFocus)
			lineedit->setFocus();
	});
	QObject::connect(this->__searchbar, &SearchBar::selectItem, [this](void) {
		PasteItem *widget = this->currentPasteItem();
		if (!widget)
			return;
		this->__current_item = nullptr;
		widget->copyData();
	});
	QObject::connect(this->__searchbar, &SearchBar::selectPlainTextItem, this, [this](void) {
		PasteItem *widget = this->currentPasteItem();
		if (widget)
			widget->copyData(true);
	});
	QObject::connect(this->__searchbar, &SearchBar::moveFocusPrevNext,
			 this, &MainWindow::move_to_prev_next_focus_widget);

	this->__menu_button = new RoundedButton(this->__main_frame);
	this->__menu_button->setObjectName("PanelMenu");
	this->__menu_button->setText(QStringLiteral("⋯"));
	this->__menu_button->setToolTip(QObject::tr("Menu"));
	this->__menu_button->setAccessibleName(QObject::tr("Menu"));
	this->__menu_button->setFixedSize(36, 36);
	this->__menu_button->setFlat(true);

	this->__scroll_widget = new QListWidget(this->__main_frame);
	this->__scroll_widget->setSelectionMode(QAbstractItemView::SingleSelection);
	this->__scroll_widget->setHorizontalScrollMode(QListWidget::ScrollPerPixel);
	this->__scroll_widget->setFlow(QListView::LeftToRight);
	this->__scroll_widget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->__scroll_widget->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->__scroll_widget->setViewMode(QListView::ListMode);
	this->__scroll_widget->setEditTriggers(QAbstractItemView::NoEditTriggers);
	this->__scroll_widget->setFrameShape(QListWidget::NoFrame);
	this->__scroll_widget->setSpacing(8);
	this->__scroll_widget->setWrapping(false);
	this->__scroll_widget->setFocusPolicy(Qt::NoFocus);
	this->__elastic_scroll = new ElasticScrollController(this->__scroll_widget);
	this->__scroll_widget->viewport()->installEventFilter(this);
	QObject::connect(this->__scroll_widget, &QListWidget::currentItemChanged, this,
		[this](QListWidgetItem *current, QListWidgetItem *previous) {
		if (previous) {
			auto *widget = qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(previous));
			if (widget)
				widget->setSelected(false);
		}
		if (current) {
			auto *widget = qobject_cast<PasteItem *>(this->__scroll_widget->itemWidget(current));
			if (widget)
				widget->setSelected(true);
		}
		this->__scroll_widget->update();
	});

	this->__empty_state = new QLabel(this->__scroll_widget->viewport());
	this->__empty_state->setObjectName("EmptyState");
	this->__empty_state->setAlignment(Qt::AlignCenter);
	this->__empty_state->setAttribute(Qt::WA_TransparentForMouseEvents);

	QLabel *brandIcon = new QLabel(this->__main_frame);
	brandIcon->setPixmap(QIcon(":/resources/pastes.svg").pixmap(QSize(30, 30), this->devicePixelRatioF()));
	brandIcon->setFixedSize(30, 30);
	QLabel *brandTitle = new QLabel(QObject::tr("Pastes"), this->__main_frame);
	brandTitle->setObjectName("BrandTitle");
	QLabel *historyTab = new RoundedLabel(QObject::tr("Clipboard"), RoundedRole::HistoryBadge, this->__main_frame);
	historyTab->setObjectName("HistoryTab");
	QHBoxLayout *hlayout = new QHBoxLayout();
	hlayout->setSpacing(10);
	hlayout->addWidget(brandIcon);
	hlayout->addWidget(brandTitle);
	hlayout->addSpacing(14);
	hlayout->addWidget(historyTab);
	this->__recording_status = new QLabel(QObject::tr("Recording paused"), this->__main_frame);
	this->__recording_status->setObjectName("RecordingStatus");
	this->__recording_status->setVisible(!this->__recording_enabled);
	hlayout->addWidget(this->__recording_status);
	hlayout->addStretch();
	hlayout->addWidget(this->__searchbar);
	hlayout->addWidget(this->__menu_button);

	/* Keep animation outside the panel's shadow effect: moving a snapshot
	 * must not invalidate and blur the entire history panel every frame. */
	this->__card_swipe = new CardSwipeOverlay(this);
	this->__card_reflow = new CardReflowOverlay(this);
	this->__history_count = new QLabel(this->__main_frame);
	this->__history_count->setObjectName("HistoryCount");
	this->__keyboard_hint = new QLabel(this->__main_frame);
	this->__keyboard_hint->setObjectName("KeyboardHint");
	this->__keyboard_hint->setVisible(QSettings().value("showKeyboardHints", true).toBool());
	this->__undo_hint = new QLabel(QObject::tr("Removed from history"), this->__main_frame);
	this->__undo_hint->setObjectName("UndoHint");
	this->__undo_button = new RoundedButton(this->__main_frame);
	this->__undo_button->setObjectName("UndoButton");
	this->__undo_button->setText(QObject::tr("Undo"));
	this->__undo_button->setToolTip(QObject::tr("Undo deletion (Ctrl+Z)"));
	this->__undo_button->setFocusPolicy(Qt::NoFocus);
	/* Align the painted button with labels, without native layout insets. */
	this->__undo_button->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	this->__undo_button->setFixedHeight(24);
	QObject::connect(this->__undo_button, &QPushButton::clicked, this, &MainWindow::undoDeletion);
	this->__undo_timer = new QTimer(this);
	this->__undo_timer->setSingleShot(true);
	QObject::connect(this->__undo_timer, &QTimer::timeout, this, [this](void) {
		this->__deleted_items.clear();
		this->updateUndoState();
	});
	this->__undo_shortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
	QObject::connect(this->__undo_shortcut, &QShortcut::activated, this, [this](void) {
		LineEdit *search = this->__searchbar->findChild<LineEdit *>();
		if (search->hasFocus() && search->isUndoAvailable()) search->undo();
		else this->undoDeletion();
	});
	this->updateUndoState();
	QHBoxLayout *footer = new QHBoxLayout();
	footer->setContentsMargins(8, 0, 8, 0);
	footer->setSpacing(12);
	footer->addWidget(this->__history_count);
	footer->addWidget(this->__undo_hint);
	footer->addWidget(this->__undo_button);
	footer->addStretch();
	footer->addWidget(this->__keyboard_hint);
	QWidget *footerWidget = new QWidget(this->__main_frame);
	footerWidget->setFixedHeight(24);
	footerWidget->setLayout(footer);

	QVBoxLayout *vlayout = new QVBoxLayout();
	vlayout->setContentsMargins(Platform::panelAppearance().margins);
	vlayout->setSpacing(Platform::panelAppearance().spacing);
	vlayout->addLayout(hlayout);
	vlayout->addWidget(this->__scroll_widget, 1);
	vlayout->addWidget(footerWidget);

	this->__main_frame->setLayout(vlayout);
	/* need this for resize this->__scroll_widget size */
	this->__main_frame->show();

	this->setupTrayIcon();
	this->updateShortcutHint();

	/* load data from database */
	this->reloadData();
}

void MainWindow::setupTrayIcon(void)
{
	QMenu *tray_menu = new QMenu(this);
	QMenu *panel_menu = new QMenu(this->__menu_button);

	if (Platform::menuAppearance().showPanelAction) {
		this->__show_action = new QAction(this);
		QObject::connect(this->__show_action, &QAction::triggered, [this](void) {
			this->show_window();
	});
	tray_menu->addAction(this->__show_action);
	tray_menu->addSeparator();
	}
	QAction *settings = Platform::createSettingsAction(this);
	settings->setObjectName("SettingsAction");
	QObject::connect(settings, &QAction::triggered, this, &MainWindow::showSettings);
	tray_menu->addAction(settings);
	panel_menu->addAction(settings);
	if (Platform::menuAppearance().preferencesSeparator)
		panel_menu->addSeparator();

	QAction *about_me = new QAction(QObject::tr("About Pastes"), this);
	QObject::connect(about_me, &QAction::triggered, [this](void) {
		AboutDialog dialog(this);
		this->execAppDialog(dialog);
	});
	tray_menu->addAction(about_me);
	panel_menu->addAction(about_me);

	tray_menu->addSeparator();
	panel_menu->addSeparator();

	QAction *quit_action = Platform::createQuitAction(this);
	QObject::connect(quit_action, &QAction::triggered, [](void) {
		qApp->quit();
	});
	tray_menu->addAction(quit_action);
	panel_menu->addAction(quit_action);
	QObject::connect(this->__menu_button, &QPushButton::clicked, this, [this, panel_menu](void) {
		Platform::popupMenu(panel_menu, this->__menu_button);
	});

	this->__tray_icon = new QSystemTrayIcon(this);
	this->__tray_icon->setIcon(QIcon(":/resources/pastes.svg"));
	this->__tray_icon->setToolTip("Pastes");
	this->__tray_icon->setContextMenu(tray_menu);
	QObject::connect(this->__tray_icon, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
		/* left click / double click toggles the window */
		if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
			if (this->isVisible())
				this->hide_window();
			else
				this->show_window();
		}
	});

	if (!QSystemTrayIcon::isSystemTrayAvailable())
		qWarning() << "Pastes: no system tray available";
	this->__tray_icon->show();
}

void MainWindow::updateTrayTooltip(void)
{
	if (!this->__tray_icon)
		return;

	this->__tray_icon->setToolTip(QString("Pastes · %1\n%2 ").arg(this->__primary_shortcut)
		.arg(this->__scroll_widget->count()) + QObject::tr("records") +
		(this->__recording_enabled ? QString() : '\n'+QObject::tr("Recording paused")));
}

void MainWindow::showSettings(void)
{
	SettingsDialog dialog(this->__primary_shortcut, this);
	QObject::connect(&dialog, &SettingsDialog::themeChanged, this, &MainWindow::applyTheme);
	QObject::connect(&dialog, &SettingsDialog::recordingChanged, this, &MainWindow::setHistoryRecording);
	QObject::connect(&dialog, &SettingsDialog::hintsChanged, this, [this](bool visible) {
		this->__keyboard_hint->setVisible(visible);
	});
	QObject::connect(this->__shortcut, &GlobalShortcut::primaryShortcutChanged, &dialog, &SettingsDialog::setPrimaryShortcut);
	this->execAppDialog(dialog);
}

void MainWindow::execAppDialog(AppDialog &dialog)
{
	/* Cover the activation events dispatched by the modal event loop,
	 * including those emitted before Qt registers the active dialog. */
	QScopedValueRollback<bool> dialogOpen(this->__app_dialog_open, true);
	if (Platform::panelAppearance().hideForDialog)
		this->hide_window();
	dialog.exec();
}

void MainWindow::setHistoryRecording(bool enabled)
{
	/* Consume any copy under the old recording state. In particular, a
	 * paused copy awaiting the next poll must not be recorded on resume. */
	this->__clipboard_source->synchronize();
	this->__recording_enabled = enabled;
	this->__clipboard_timer->stop();
	++this->__source_request;
	this->__source_icon = QImage();
	this->__recording_status->setVisible(!enabled);
	this->updateTrayTooltip();
}

void MainWindow::updateShortcutHint(void)
{
	if (this->__keyboard_hint)
		this->__keyboard_hint->setText(QObject::tr("%1 Open   ·   ← → Browse   ·   Drag ↑ Delete   ·   Enter Paste   ·   Space Preview")
			.arg(this->__primary_shortcut));
	if (this->__show_action)
		this->__show_action->setText(QObject::tr("Show (%1)").arg(this->__primary_shortcut));
	this->updateTrayTooltip();
}

void MainWindow::reloadData()
{
	QObject::connect(&this->__db, SIGNAL(dataLoaded(QList<ItemData *>)), this, SLOT(parsingData(QList<ItemData *>)));
	this->__db.loadData();
}

void MainWindow::parsingData(QList<ItemData *> list)
{
	const QDateTime now = QDateTime::currentDateTime();
	for (auto itemData : list) {
		if (HistoryPolicy::expired(itemData->time, now)) {
			this->__db.deletePasteItem(itemData);
			continue;
		}

		PasteItem *widget = this->insertItemWidget(true);
		bool hasContent = false;

		QList<QUrl> urls = itemData->mimeData->urls();
		bool localFiles = !urls.isEmpty();
		for (const QUrl &url : urls)
			localFiles &= url.isLocalFile();
		/* Match new copies and Undo: file references precede ancillary HTML
		 * and bitmap flavors, with a fallback when the files are gone. */
		if (localFiles && widget->setUrls(urls)) {
			hasContent = true;
		} else if (itemData->mimeData->hasHtml() && !itemData->mimeData->text().isEmpty()) {
			widget->setRichText(itemData->mimeData->html(), itemData->mimeData->text());
			hasContent = true;
		} else if (itemData->mimeData->hasImage() && widget->setImage(itemData->mimeData)) {
			hasContent = true;
		} else if (itemData->mimeData->hasUrls()) {
			hasContent = !localFiles && widget->setUrls(urls);
		} else if (itemData->mimeData->hasText() && !itemData->mimeData->text().isEmpty()) {
			widget->setPlainText(itemData->mimeData->text().trimmed());
			hasContent = true;
		}

		if (!hasContent) {
			/* No displayable data, remove it from the UI and the database,
			 * otherwise the empty row is reloaded on every start */
			this->__scroll_widget->removeItemWidget(widget->widgetItem());
			delete widget->widgetItem();
			delete widget;
			this->__db.deletePasteItem(itemData);
			continue;
		}

		widget->setTime(itemData->time);
		QPixmap icon = QPixmap::fromImage(itemData->icon);
		widget->setIcon(icon);
		widget->widgetItem()->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<uint64_t>(itemData)));
	}

	this->__scroll_widget->setCurrentRow(0);
	this->resetItemTabOrder();
	this->updateQuickPasteNumbers();
	this->updateTrayTooltip();

	/* Need create window init time, it's speed up for show */
	this->setVisible(true);
	this->setVisible(false);
}

void MainWindow::applyTheme(const QString &name)
{
	if (this->__scroll_widget) this->resetPointerGesture();
	qApp->setProperty("pastesDark", name != "light");
	QString file = (name == "light") ? ":/resources/theme-light.qss"
					 : ":/resources/theme-dark.qss";

	QFile qss(file);
	qss.open(QFile::ReadOnly);
	if (qss.isOpen()) {
		/* replace (not append) so switching themes at runtime works */
		this->setStyleSheet(QString::fromUtf8(qss.readAll()));
		qss.close();
	} else {
		qWarning() << "Pastes: cannot find theme file" << file;
	}

	this->__theme = (name == "light") ? "light" : "dark";
	QSettings().setValue("theme", this->__theme);
	Platform::updatePanelBackdrop(this);
}

/* Insert a PasteItem into listwidget */
PasteItem *MainWindow::insertItemWidget(bool back, int row)
{
	QListWidgetItem *item = new QListWidgetItem;
	auto *widget = new PasteItem(nullptr, item);

	QObject::connect(widget, &PasteItem::hideWindow, [this](void) {
		this->hide_window();
	});
	QObject::connect(widget, &PasteItem::moveFocusPrevNext,
			 this, &MainWindow::move_to_prev_next_focus_widget);
	QObject::connect(widget, &PasteItem::clipboardUpdated, this, [this](void) {
		/* Internal copies use the same debounce to refresh and promote the
		 * entry even when Qt delivered dataChanged synchronously. */
		if (this->__recording_enabled)
			this->__clipboard_timer->start();
	});
	QObject::connect(widget, &PasteItem::copied, this, [this](bool hasUrls) {
		this->pasteToPreviousWindow(hasUrls);
	});
	QObject::connect(widget, &PasteItem::previewRequested, this, [this, widget](void) {
		this->__scroll_widget->setCurrentItem(widget->widgetItem());
		this->previewCurrentItem();
	});
	QObject::connect(widget, &PasteItem::deleteRequested, this, [this, widget](void) {
		this->__scroll_widget->setCurrentItem(widget->widgetItem());
		this->deleteCurrentItem();
	});

	/* resize item, It's use for pasteitem frame */
	item->setSizeHint(this->cardSize());

	if (row >= 0) {
		this->__scroll_widget->insertItem(row, item);
	} else if (back) {
		this->__scroll_widget->addItem(item);
	} else {
		this->__scroll_widget->insertItem(0, item);
		this->__scroll_widget->setCurrentRow(0);
	}
	this->__scroll_widget->setItemWidget(item, widget);
	widget->setSelected(item->isSelected());

	return widget;
}

QSize MainWindow::cardSize(void) const
{
	return Platform::cardSize(this->size());
}

void MainWindow::resetItemTabOrder(void)
{
	int count = this->__scroll_widget->count();

	for (int i = 0; i < count-1; i++) {
		QWidget *first = this->__scroll_widget->itemWidget(this->__scroll_widget->item(i));
		QWidget *second = this->__scroll_widget->itemWidget(this->__scroll_widget->item(i+1));
		QWidget::setTabOrder(first, second);
	}
}

void MainWindow::clipboard_later(void)
{
	if (!this->__recording_enabled)
		return;
	/* A newer native copy schedules its own snapshot with its own source. */
	if (this->__clipboard_source->synchronize())
		return;
	this->resetPointerGesture();
	this->__last_clicked_item.clear();
	const quint64 sourceRequest = this->__source_request;
	const QImage sourceIcon = this->__source_icon;
	const QMimeData *mime_data = QApplication::clipboard()->mimeData();
	const QVariant copiedIcon = mime_data->property("pastesSourceIcon");
	PasteItem *widget = nullptr;
	QCryptographicHash hash(QCryptographicHash::Md5);
	ItemData *itemData = new ItemData;

	itemData->mimeData = dup_mimedata(mime_data);
	/* Reading a promised flavor can change the pasteboard. Retry through
	 * the new notification instead of saving a mixed or outdated snapshot. */
	if (this->__clipboard_source->synchronize()) {
		delete itemData->mimeData;
		delete itemData;
		return;
	}
	widget = this->insertItemWidget(false);

	do {
		if (itemData->mimeData->hasUrls() && !itemData->mimeData->urls().isEmpty()) {
			QList<QUrl> urls = itemData->mimeData->urls();
			for (const QUrl &url : urls) {
				hash.addData(url.toEncoded());
			}
			widget->setUrls(urls);
			qDebug() << "It's urls";
			break;
		}

		if (itemData->mimeData->hasHtml() && !itemData->mimeData->text().trimmed().isEmpty()) {
			hash.addData(itemData->mimeData->text().trimmed().toLocal8Bit());
			widget->setRichText(itemData->mimeData->html(), itemData->mimeData->text().trimmed());
			qDebug() << "It's htmls";
			break;
		}

		if (itemData->mimeData->hasImage()) {
			QImage image = qvariant_cast<QImage>(itemData->mimeData->imageData());
			if (image.width() && image.height()) {
				/* Hash the raw pixel rows. The old code copied one byte at a
				 * time and only covered a quarter of the row (one channel of
				 * each ARGB pixel), which was both slow and a weak digest. */
				for (int row = 0; row < image.height(); ++row) {
					hash.addData(reinterpret_cast<const char *>(image.constScanLine(row)),
						     image.bytesPerLine());
				}

				widget->setImage(image);
				qDebug() << "It's Images" << image.sizeInBytes();
				break;
			}
		}

		if (itemData->mimeData->hasText() && !itemData->mimeData->text().trimmed().isEmpty()) {
			hash.addData(itemData->mimeData->text().trimmed().toLocal8Bit());
			widget->setPlainText(itemData->mimeData->text().trimmed());
			qDebug() << "It's text";
			break;
		}

		qDebug() << "It's nothing";
		/* No data, remove it */
		QListWidgetItem *tmp_item = this->__scroll_widget->item(0);
		this->__scroll_widget->removeItemWidget(tmp_item);
		if (this->__current_item == tmp_item)
			this->__current_item = nullptr;
		delete tmp_item;
		delete itemData->mimeData;
		delete itemData;
		return;
	} while (0);

	itemData->md5 = hash.result();
	if (copiedIcon.isValid()) {
		itemData->icon = copiedIcon.value<QImage>();
	} else {
		itemData->icon = sourceIcon.isNull() ? this->__clipboard_source->snapshotIcon() : sourceIcon;
	}
	/* Remove dup item */
	const QDateTime now = QDateTime::currentDateTime();
	for (int i = 1; i < this->__scroll_widget->count(); i++) {
		QListWidgetItem *tmp_item = this->__scroll_widget->item(i);
		ItemData *tmp_itemData = reinterpret_cast<ItemData *>(tmp_item->data(Qt::UserRole).value<uint64_t>());
		if (!tmp_itemData)
			continue;
		/* They have same md5, remove it */
		if (itemData->md5 == tmp_itemData->md5) {
			/* Capture the value before the worker takes ownership of the
			 * duplicate. A failed lookup must not erase a known source icon. */
			if (itemData->icon.isNull())
				itemData->icon = tmp_itemData->icon;
			tmp_item->setData(Qt::UserRole, QVariant());
			this->__db.deletePasteItem(tmp_itemData);
			this->__scroll_widget->removeItemWidget(tmp_item);
			if (this->__current_item == tmp_item)
				this->__current_item = nullptr;
			delete tmp_item;
			--i;
			continue;
		}
		if (HistoryPolicy::expired(tmp_itemData->time, now)) {
			tmp_item->setData(Qt::UserRole, QVariant());
			this->__db.deletePasteItem(tmp_itemData);
			this->__scroll_widget->removeItemWidget(tmp_item);
			if (this->__current_item == tmp_item)
				this->__current_item = nullptr;
			delete tmp_item;
			--i;
		}
	}

	itemData->time = now;
	widget->setTime(itemData->time);

	/* Internal copies have their original icon and must not be overwritten
	 * by a late lookup for the Pastes window that performed the copy. */
	widget->setProperty("sourceRequest", QVariant::fromValue(copiedIcon.isValid() ? quint64(0) : sourceRequest));
	QPixmap icon = QPixmap::fromImage(itemData->icon);
	widget->setIcon(icon);
	widget->widgetItem()->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<uint64_t>(itemData)));
	widget->setProperty("imageRequest", QVariant::fromValue(this->__db.insertPasteItem(itemData)));
	this->resetItemTabOrder();
	this->updateQuickPasteNumbers();
	this->updateTrayTooltip();
}
