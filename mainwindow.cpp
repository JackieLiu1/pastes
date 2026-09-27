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
#include <QFileInfo>
#include <QFileIconProvider>
#include <QUrl>
#include <QBuffer>
#include <QMenu>
#include <QAction>
#include <QMutex>
#include <QMutexLocker>
#include <QShortcut>
#include <QEvent>
#include <QDebug>
#include <QSystemTrayIcon>
#include <QSettings>
#include <QCursor>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPointer>
#include <QHash>

#include "mainwindow.h"
#include "pasteitem.h"
#include "previewdialog.h"
#include "appdialog.h"
#include "settingsdialog.h"
#include "cardswipe.h"
#include "cardreflow.h"
#include "elasticscroll.h"

/* History older than this is dropped on startup and on every clipboard update */
static const qint64 MAX_HISTORY_SECS = 7 * 24 * 60 * 60;
/* Wait for the clipboard to settle before snapshotting it: rapid format
 * updates from one copy collapse into a single entry */
static const int CLIPBOARD_SETTLE_MS = 1000;

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <winuser.h>
#include <shellapi.h>
#include <comdef.h>
#include <commctrl.h>
#include <objbase.h>
#include <commoncontrols.h>
#include "clipboardsource_win.h"
#include <QOperatingSystemVersion>
#endif

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
/* KWindowEffects only exists in the KF5 (Qt5) module */
#include <KF5/KWindowSystem/KWindowEffects>
#define PASTES_HAVE_KWINDOWEFFECTS 1
#endif
#endif

#ifdef Q_OS_WIN
typedef enum _WINDOWCOMPOSITIONATTRIB
{
	WCA_UNDEFINED = 0,
	WCA_NCRENDERING_ENABLED = 1,
	WCA_NCRENDERING_POLICY = 2,
	WCA_TRANSITIONS_FORCEDISABLED = 3,
	WCA_ALLOW_NCPAINT = 4,
	WCA_CAPTION_BUTTON_BOUNDS = 5,
	WCA_NONCLIENT_RTL_LAYOUT = 6,
	WCA_FORCE_ICONIC_REPRESENTATION = 7,
	WCA_EXTENDED_FRAME_BOUNDS = 8,
	WCA_HAS_ICONIC_BITMAP = 9,
	WCA_THEME_ATTRIBUTES = 10,
	WCA_NCRENDERING_EXILED = 11,
	WCA_NCADORNMENTINFO = 12,
	WCA_EXCLUDED_FROM_LIVEPREVIEW = 13,
	WCA_VIDEO_OVERLAY_ACTIVE = 14,
	WCA_FORCE_ACTIVEWINDOW_APPEARANCE = 15,
	WCA_DISALLOW_PEEK = 16,
	WCA_CLOAK = 17,
	WCA_CLOAKED = 18,
	WCA_ACCENT_POLICY = 19,
	WCA_FREEZE_REPRESENTATION = 20,
	WCA_EVER_UNCLOAKED = 21,
	WCA_VISUAL_OWNER = 22,
	WCA_LAST = 23
} WINDOWCOMPOSITIONATTRIB;

typedef struct _WINDOWCOMPOSITIONATTRIBDATA
{
	WINDOWCOMPOSITIONATTRIB Attrib;
	PVOID pvData;
	SIZE_T cbData;
} WINDOWCOMPOSITIONATTRIBDATA;

typedef enum _ACCENT_STATE
{
	ACCENT_DISABLED = 0,
	ACCENT_ENABLE_GRADIENT = 1,
	ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
	ACCENT_ENABLE_BLURBEHIND = 3,
	ACCENT_INVALID_STATE = 4
} ACCENT_STATE;

typedef struct _ACCENT_POLICY
{
	ACCENT_STATE AccentState;
	DWORD AccentFlags;
	DWORD GradientColor;
	DWORD AnimationId;
} ACCENT_POLICY;

typedef BOOL (WINAPI *pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
#endif

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent),
	  __main_frame(new MainFrame(this)),
	  __main_frame_shadow(new QGraphicsDropShadowEffect(this)),
	  __hide_animation(new QPropertyAnimation(this, "pos")),
	  __shortcut(new GlobalShortcut(this)),
	  __hide_state(true),
	  __current_item(nullptr)
{
	QRect rect = QApplication::primaryScreen()->availableGeometry();
	this->__recording_enabled = !QSettings().value("pauseRecording", false).toBool();

	const int panelHeight = qMin(rect.height(), qBound(300, rect.height()*38/100, 450));
	this->setFixedHeight(panelHeight);
	this->setGeometry(rect.x(), rect.bottom()-panelHeight+1, rect.width(), panelHeight);
	this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
			     Qt::BypassWindowManagerHint | Qt::SplashScreen);
	this->setFocusPolicy(Qt::NoFocus);
	this->setFont(QFont(QStringLiteral("Segoe UI"), 10));
	this->applyTheme(QSettings().value("theme", "light").toString());
	this->setCentralWidget(this->__main_frame);
#if !defined Q_OS_LINUX && !defined Q_OS_WIN
	this->setContentsMargins(0, 10, 0, 0);
#endif
	this->setAttribute(Qt::WA_TranslucentBackground, true);
	this->enabledGlassEffect();

	this->__main_frame->setGeometry(this->geometry());
	this->__main_frame_shadow->setOffset(0, 0);
	this->__main_frame_shadow->setColor(QColor(0, 0, 0, 130));
	this->__main_frame_shadow->setBlurRadius(24);
	this->__main_frame->setGraphicsEffect(this->__main_frame_shadow);
	this->__main_frame->setFocusPolicy(Qt::ClickFocus);
	QObject::connect(this->__main_frame, SIGNAL(moveFocusPrevNext(bool)), this, SLOT(move_to_prev_next_focus_widget(bool)));
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
	this->__clipboard_timer->setInterval(CLIPBOARD_SETTLE_MS);
	QObject::connect(this->__clipboard_timer, &QTimer::timeout, this, &MainWindow::clipboard_later);
#ifdef Q_OS_WIN
	this->__clipboard_source = new ClipboardSource(this);
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
#endif
	QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [this](void) {
		if (!this->__recording_enabled)
			return;
#ifdef Q_OS_WIN
		const QVariant copiedIcon = QApplication::clipboard()->mimeData()->property("pastesSourceIcon");
		++this->__source_request;
		this->__source_icon = copiedIcon.value<QImage>();
		if (!copiedIcon.isValid())
			this->__clipboard_source->capture(this->__source_request);
#endif
		/* Restarting on every change collapses rapid clipboard updates
		 * into one snapshot taken once the clipboard has settled. */
		this->__clipboard_timer->start();
	});
	QObject::connect(this->__hide_animation, &QPropertyAnimation::finished, [this](void) {
		if (this->__hide_animation->direction() == QAbstractAnimation::Forward) {
			/* Hidden stage */
			this->hide();
		}
	});
	this->__hide_animation->setDuration(200);
	this->__hide_animation->setStartValue(this->pos());
	this->__hide_animation->setEndValue(QPoint(rect.x(), rect.bottom()+1));
	this->__hide_animation->setEasingCurve(QEasingCurve::OutQuad);

	QObject::connect(this->__shortcut, &GlobalShortcut::pasteActivated, [this](void) {
		if (!this->__hide_state)
			this->hide_window();
		else
			this->show_window();
	});
#ifdef Q_OS_WIN
	this->__primary_shortcut = QStringLiteral("Win+V");
#elif defined(Q_OS_MACOS)
	this->__primary_shortcut = QStringLiteral("Shift+Cmd+V");
#else
	this->__primary_shortcut = QStringLiteral("Ctrl+Shift+V");
#endif
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
}

bool MainWindow::event(QEvent *e)
{
	if (e->type() == QEvent::ActivationChange) {
		QWidget *active = QApplication::activeWindow();
		if (active != this && !(active && this->isAncestorOf(active)))
			this->hide_window();
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
	if (type == QEvent::MouseMove && this->__mouse_down) {
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
	if (!this->__scroll_widget)
		return;
	this->resetPointerGesture();
	const int cardWidth = qBound(210, this->width()/6, 280);
	const int cardHeight = qMax(110, this->height()-136);
	for (int i = 0; i < this->__scroll_widget->count(); ++i)
		this->__scroll_widget->item(i)->setSizeHint(QSize(cardWidth, cardHeight));
	if (this->__empty_state)
		this->__empty_state->setGeometry(this->__scroll_widget->viewport()->rect());
}

void MainWindow::show_window(void)
{
	QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
	if (!screen)
		screen = QApplication::primaryScreen();
	const QRect area = screen->availableGeometry();
	const int panelHeight = qMin(area.height(), qBound(300, area.height()*38/100, 450));
	this->setFixedHeight(panelHeight);
	this->setGeometry(area.x(), area.bottom()-panelHeight+1, area.width(), panelHeight);
	this->__hide_animation->setStartValue(this->pos());
	this->__hide_animation->setEndValue(QPoint(area.x(), area.bottom()+1));
#ifdef Q_OS_WIN
	HWND target = GetForegroundWindow();
	this->__paste_target = (target && target != reinterpret_cast<HWND>(this->winId()))
				 ? reinterpret_cast<quintptr>(target) : 0;
#endif
	this->__hide_animation->setDirection(QAbstractAnimation::Backward);
	this->__hide_animation->start();
	this->__hide_state = false;
	this->show();
	this->activateWindow();
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

void MainWindow::pasteToPreviousWindow(void)
{
#ifdef Q_OS_WIN
	const HWND target = reinterpret_cast<HWND>(this->__paste_target);
	if (!target || !IsWindow(target))
		return;
	DWORD owner = 0;
	GetWindowThreadProcessId(target, &owner);
	if (owner == GetCurrentProcessId())
		return;
	wchar_t className[64] = {};
	GetClassNameW(target, className, 64);
	if (lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
	    lstrcmpW(className, L"NotifyIconOverflowWindow") == 0)
		return;
	QTimer::singleShot(300, this, [target](void) {
		if (!IsWindow(target) || !SetForegroundWindow(target) || GetForegroundWindow() != target)
			return;
		INPUT input[4] = {};
		input[0].type = input[1].type = input[2].type = input[3].type = INPUT_KEYBOARD;
		input[0].ki.wVk = VK_CONTROL;
		input[1].ki.wVk = 'V';
		input[2].ki.wVk = 'V';
		input[2].ki.dwFlags = KEYEVENTF_KEYUP;
		input[3].ki.wVk = VK_CONTROL;
		input[3].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(4, input, sizeof(INPUT));
	});
#endif
}

void MainWindow::move_to_prev_next_focus_widget(bool prev)
{
	this->__elastic_scroll->cancel();
	if (this->__card_swipe->sourceCard()) this->__card_swipe->cancel();
	this->__card_reflow->cancel();
	const int count = this->__scroll_widget->count();
	if (count == 0)
		return;

	int row = this->__scroll_widget->currentRow();
	if (row < 0)
		row = prev ? 0 : -1;
	const bool fromSearch = this->__searchbar->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly)->hasFocus();

	/* Bounded by the item count: if every item is hidden (search filtered
	 * everything out) this gives up instead of looping forever. */
	PasteItem *widget = nullptr;
	for (int i = 0; i < count; i++) {
		if (prev) {
			/* Get prev focus widget and isn't hidden */
			if (--row < 0)
				row = count - 1;
		} else if (i > 0 || !fromSearch || row < 0) {
			/* Get next focus widget and isn't hidden */
			if (++row > count - 1)
				row = 0;
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
	QPointer<PasteItem> widget = this->currentPasteItem();
	PreviewDialog dialog(*data, this);
	QObject::connect(&dialog, &PreviewDialog::copyRequested, this, [widget](void) {
		if (widget) widget->copyData(false, false);
	});
	QObject::connect(widget, &QObject::destroyed, &dialog, &QDialog::reject);
	if (dialog.exec() == QDialog::Accepted && widget)
		widget->copyData(dialog.plainText());
	else if (widget)
		widget->setFocus();
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
			QImage image = qvariant_cast<QImage>(mime->imageData());
			restored->setImage(image);
		} else {
			restored->setPlainText(mime->text().trimmed());
		}
		restored->setTime(data->time);
		restored->setIcon(QPixmap::fromImage(data->icon));
		restored->widgetItem()->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<uint64_t>(data)));
		this->__db.insertPasteItem(data);
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
	QObject::connect(this->__searchbar, SIGNAL(moveFocusPrevNext(bool)), this, SLOT(move_to_prev_next_focus_widget(bool)));

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
	vlayout->setContentsMargins(24, 16, 24, 12);
	vlayout->setSpacing(10);
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

	this->__show_action = new QAction(this);
	QObject::connect(this->__show_action, &QAction::triggered, [this](void) {
		this->show_window();
	});
	tray_menu->addAction(this->__show_action);
	tray_menu->addSeparator();
	QAction *settings = new QAction(QObject::tr("Settings"), this);
	settings->setObjectName("SettingsAction");
	QObject::connect(settings, &QAction::triggered, this, &MainWindow::showSettings);
	tray_menu->addAction(settings);
	panel_menu->addAction(settings);

	QAction *about_me = new QAction(QObject::tr("About Pastes"), this);
	QObject::connect(about_me, &QAction::triggered, [this](void) {
		AboutDialog dialog(this);
		dialog.exec();
	});
	tray_menu->addAction(about_me);
	panel_menu->addAction(about_me);

	tray_menu->addSeparator();
	panel_menu->addSeparator();

	QAction *quit_action = new QAction(QObject::tr("Quit"), this);
	QObject::connect(quit_action, &QAction::triggered, [](void) {
		qApp->quit();
	});
	tray_menu->addAction(quit_action);
	panel_menu->addAction(quit_action);
	QObject::connect(this->__menu_button, &QPushButton::clicked, this, [this, panel_menu](void) {
		panel_menu->exec(this->__menu_button->mapToGlobal(
			QPoint(0, this->__menu_button->height())));
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
	dialog.exec();
}

void MainWindow::setHistoryRecording(bool enabled)
{
	this->__recording_enabled = enabled;
	this->__clipboard_timer->stop();
#ifdef Q_OS_WIN
	++this->__source_request;
	this->__source_icon = QImage();
#endif
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
	for (auto itemData : list) {
		/* remove the data if it's too old (than a week) */
		if (QDateTime::currentDateTime().toSecsSinceEpoch() - itemData->time.toSecsSinceEpoch() > MAX_HISTORY_SECS) {
			this->__db.deletePasteItem(itemData);
			continue;
		}

		PasteItem *widget = this->insertItemWidget(true);
		bool hasContent = false;

		if (itemData->mimeData->hasHtml() && !itemData->mimeData->text().isEmpty()) {
			widget->setRichText(itemData->mimeData->html(), itemData->mimeData->text());
			hasContent = true;
		} else if (itemData->mimeData->hasImage() && itemData->mimeData->imageData().isValid() &&
			   !itemData->mimeData->imageData().isNull()) {
			QImage image = qvariant_cast<QImage>(itemData->mimeData->imageData());
			widget->setImage(image);
			hasContent = true;
		} else if (itemData->mimeData->hasUrls()) {
			QList<QUrl> urls = itemData->mimeData->urls();
			hasContent = widget->setUrls(urls);
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
	QObject::connect(widget, &PasteItem::copied, this, [this](void) {
		this->pasteToPreviousWindow();
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
	item->setSizeHint(QSize(qBound(210, this->width()/6, 280),
				    qMax(110, this->height()-136)));

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
	this->resetPointerGesture();
	this->__last_clicked_item.clear();
#ifdef Q_OS_WIN
	const quint64 sourceRequest = this->__source_request;
	const QImage sourceIcon = this->__source_icon;
#endif
	const QMimeData *mime_data = QApplication::clipboard()->mimeData();
	const QVariant copiedIcon = mime_data->property("pastesSourceIcon");
	PasteItem *widget = nullptr;
	QCryptographicHash hash(QCryptographicHash::Md5);
	ItemData *itemData = new ItemData;

	itemData->mimeData = dup_mimedata(mime_data);
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
#ifdef Q_OS_WIN
		itemData->icon = sourceIcon;
#else
		itemData->icon = this->getClipboardOwnerIcon().scaled(32, 32,
			Qt::KeepAspectRatio, Qt::SmoothTransformation).toImage();
#endif
	}
	/* Remove dup item */
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
			this->__db.deletePasteItem(tmp_itemData);
			this->__scroll_widget->removeItemWidget(tmp_item);
			if (this->__current_item == tmp_item)
				this->__current_item = nullptr;
			delete tmp_item;
			continue;
		}
		/* remove the data if it's too old (than a week) */
		if (QDateTime::currentDateTime().toSecsSinceEpoch() - tmp_itemData->time.toSecsSinceEpoch() >= MAX_HISTORY_SECS) {
			this->__db.deletePasteItem(tmp_itemData);
			this->__scroll_widget->removeItemWidget(tmp_item);
			if (this->__current_item == tmp_item)
				this->__current_item = nullptr;
			delete tmp_item;
		}
	}

	itemData->time = QDateTime::currentDateTime();
	widget->setTime(itemData->time);

#ifdef Q_OS_WIN
	/* Internal copies have their original icon and must not be overwritten
	 * by a late lookup for the Pastes window that performed the copy. */
	widget->setProperty("sourceRequest", QVariant::fromValue(copiedIcon.isValid() ? quint64(0) : sourceRequest));
#endif
	QPixmap icon = QPixmap::fromImage(itemData->icon);
	widget->setIcon(icon);
	widget->widgetItem()->setData(Qt::UserRole, QVariant::fromValue(reinterpret_cast<uint64_t>(itemData)));
	this->__db.insertPasteItem(itemData);
	this->resetItemTabOrder();
	this->updateQuickPasteNumbers();
	this->updateTrayTooltip();
}

#ifdef Q_OS_LINUX
static bool get_window_name2(Display* dpy, Window window, char* buf)
{
	XTextProperty tp;

	XGetTextProperty(dpy, window, &tp, XInternAtom(dpy, "WM_NAME", False));
	if (tp.nitems > 0) {
		int count = 0, i, ret;
		char **list = NULL;

		ret = XmbTextPropertyToTextList(dpy, &tp, &list, &count);
		if((ret == Success || ret > 0) && list != NULL){
			for(i=0; i<count; i++)
				snprintf(buf, 1024, "%s", list[i]);
			XFreeStringList(list);
		} else {
			snprintf(buf, 1024, "%s", tp.value);
		}

		return true;
	} else {
		return false;
	}
}

static QString strip_cmd(QString window_title)
{
	if (window_title.contains("Qt Selection Owner")) {
		return window_title.mid(23);
	} else if (window_title.contains("Chromium ")) {
		return "chrome";
	}

	return window_title;
}
#endif

QPixmap MainWindow::getClipboardOwnerIcon(void)
{
	QPixmap pixmap;

#ifdef Q_OS_LINUX
	int i = 0;
	Display *display = XOpenDisplay(NULL);
	Atom clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
	Window clipboard_owner_win = XGetSelectionOwner(display, clipboard_atom);
	char buf[1024] = {0};
	unsigned long nitems, bytesafter;
	unsigned char *ret;
	int format;
	Atom type;
	Atom wm_icon_atom = XInternAtom(display, "_NET_WM_ICON", True);
	qDebug() << clipboard_owner_win;
	/* Get clipboard owner title name */
	get_window_name2(display, clipboard_owner_win, buf);
	QString command = strip_cmd(buf);
	qDebug() << buf << command;

	/* Search from [-100, 100] */
	clipboard_owner_win -= 100;
again:
	/* Get the width of the icon */
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   0, 1, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		/* FIXME: In fact, Get clipboard window id from XLIB is not the
		 * actual window id, but it is strange that his actual ID is
		 * near this, between -100 and +100.
		 *
		 * I didn't find out what happened, but he seems to be working.
		 * if anyone finds a good way, please let me know.
		 */
		clipboard_owner_win++;
		if (i++ > 200) {
			XCloseDisplay(display);
			qDebug() << "Not found icon, Use default Linux logo";
			pixmap.convertFromImage(QImage(":/resources/ubuntu.png"));
			return pixmap;
		}

		goto again;
	}

	int width = *(int *)ret;
	XFree(ret);

	/* Get the height of the Icon */
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   1, 1, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		qDebug() << "No X11 Icon height Found.";
		return pixmap;
	}

	int height = *(int *)ret;
	XFree(ret);

	/* Get data from Icon */
	int size = width * height;
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   2, size, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		qDebug() << "No X11 Icon Data Found.";
		return pixmap;
	}

	unsigned long *imgArr = (unsigned long*)(ret);
	std::vector<uint32_t> imgARGB32(size);
	for(int i=0; i<size; ++i)
		imgARGB32[i] = (uint32_t)(imgArr[i]);

	QImage *image = new QImage((uchar*)imgARGB32.data(), width, height, QImage::Format_ARGB32);
	pixmap.convertFromImage(*image);

	XFree(ret);
	delete image;
	XCloseDisplay(display);
#endif

	return pixmap;
}

void MainWindow::enabledGlassEffect(void)
{
#ifdef Q_OS_WIN
	if (QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10) {
		HWND hWnd = HWND(this->winId());
		HMODULE hUser = GetModuleHandle(L"user32.dll");
		if (hUser) {
			pfnSetWindowCompositionAttribute setWindowCompositionAttribute =
					(pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
			if (setWindowCompositionAttribute) {
				ACCENT_POLICY accent = { ACCENT_ENABLE_BLURBEHIND, 0, 0, 0 };
				WINDOWCOMPOSITIONATTRIBDATA data;
				data.Attrib = WCA_ACCENT_POLICY;
				data.pvData = &accent;
				data.cbData = sizeof(accent);
				setWindowCompositionAttribute(hWnd, &data);
			}
		}
	}
#endif

#ifdef PASTES_HAVE_KWINDOWEFFECTS
	KWindowEffects::enableBlurBehind(this->winId(), true);
#endif
}
