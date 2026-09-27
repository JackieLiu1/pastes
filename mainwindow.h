#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "mainframe.h"
#include "pasteitem.h"
#include "shortcut.h"
#include "searchbar.h"
#include "database.h"

#include <QMainWindow>
#include <QPropertyAnimation>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QListWidget>
#include <QListWidgetItem>
#include <QClipboard>
#include <QTimer>
#include <QPointer>
#include <QElapsedTimer>
#include <memory>
#include <vector>

class QSystemTrayIcon;
class QLabel;
class QAction;
class QShortcut;
class CardSwipeOverlay;
class CardReflowOverlay;
class ElasticScrollController;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
class ClipboardSource;
#endif
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow() = default;

protected:
	bool event(QEvent *e);
	bool eventFilter(QObject *object, QEvent *event);
	void showEvent(QShowEvent *event);
	void hideEvent(QHideEvent *event);
	void resizeEvent(QResizeEvent *event);

private:
	void initUI(void);
	void setupTrayIcon(void);
	void updateTrayTooltip(void);
	void updateShortcutHint(void);
	void applyTheme(const QString &name);
	void showSettings(void);
	void setHistoryRecording(bool enabled);
	void reloadData(void);
	PasteItem *insertItemWidget(bool, int row = -1);
	QSize cardSize(void) const;
	void resetItemTabOrder(void);
	void updateQuickPasteNumbers(void);
	void pasteNumberedItem(int number, bool plainText);
	void previewCurrentItem(void);
	void deleteCurrentItem(void);
	void undoDeletion(void);
	void updateUndoState(void);
	bool handlePointerEvent(QObject *object, QEvent *event);
	void resetPointerGesture(bool cancelSwipe = true, bool cancelReflow = true, bool cancelScroll = true);
	PasteItem *currentPasteItem(void);
	void pasteToPreviousWindow(void);
	QPixmap getClipboardOwnerIcon(void);
	void enabledGlassEffect(void);

public slots:
	void hide_window(void);
	void show_window(void);
	void clipboard_later(void);
	void move_to_prev_next_focus_widget(bool);
	void parsingData(QList<ItemData *> list);

private:
	MainFrame			*__main_frame;
	QGraphicsDropShadowEffect	*__main_frame_shadow;
	QPropertyAnimation		*__hide_animation;
	GlobalShortcut			*__shortcut;
	/* That is a workaround for hide window */
	bool				__hide_state;
	Database			__db;
	/* Debounces rapid clipboard updates into one snapshot */
	QTimer				*__clipboard_timer;

	/* widgets */
	SearchBar			*__searchbar = nullptr;
	QLabel				*__history_count = nullptr;
	QLabel				*__keyboard_hint = nullptr;
	QLabel				*__recording_status = nullptr;
	QLabel				*__empty_state = nullptr;
	QPushButton			*__menu_button;
	QListWidget			*__scroll_widget = nullptr;
	quintptr			__paste_target = 0;
	QString				__primary_shortcut;
	QPointer<PasteItem>		__pressed_item;
	QPointer<PasteItem>		__last_clicked_item;
	QElapsedTimer			__last_click_time;
	QPoint				__mouse_press;
	bool				__mouse_down = false;
	bool				__mouse_moved = false;
	enum class PointerGesture { Pending, Browse, Dismiss, Cancelled };
	PointerGesture			__pointer_gesture = PointerGesture::Pending;
	CardSwipeOverlay		*__card_swipe = nullptr;
	CardReflowOverlay		*__card_reflow = nullptr;
	ElasticScrollController		*__elastic_scroll = nullptr;
	struct DeletedEntry {
		struct Neighbor {
			QByteArray md5;
			QDateTime time;
		};
		std::unique_ptr<QMimeData> mime;
		QImage icon;
		QByteArray md5;
		QDateTime time;
		std::vector<Neighbor> neighbors;
		int row;
		quint64 dismissalId = 0;
	};
	std::vector<DeletedEntry>		__deleted_items;
	QTimer				*__undo_timer = nullptr;
	QShortcut			*__undo_shortcut = nullptr;
	QPushButton			*__undo_button = nullptr;
	QLabel				*__undo_hint = nullptr;

	/* system tray entry (bottom-right corner) */
	QSystemTrayIcon			*__tray_icon = nullptr;
	QAction				*__show_action = nullptr;

	/* current theme: "dark" or "light" (persisted in QSettings) */
	QString				__theme;
	bool				__recording_enabled = true;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
	ClipboardSource			*__clipboard_source;
	quint64				__source_request = 0;
	QImage				__source_icon;
#endif

	/* Use for store current row when searching. Not a QObject, so it can't
	 * be a QPointer: clipboard_later() resets it when it deletes the item. */
	QListWidgetItem			*__current_item = nullptr;
};
#endif // MAINWINDOW_H
