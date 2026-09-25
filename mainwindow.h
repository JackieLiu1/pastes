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

class QSystemTrayIcon;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow() = default;

protected:
	bool event(QEvent *e);
	void showEvent(QShowEvent *event);
	void hideEvent(QHideEvent *event);

private:
	void initUI(void);
	void setupTrayIcon(void);
	void updateTrayTooltip(void);
	void reloadData(void);
	PasteItem *insertItemWidget(bool);
	void resetItemTabOrder(void);
	PasteItem *currentPasteItem(void);
	static void loadStyleSheet(QWidget *, const QString &);
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
	DoubleCtrlShortcut		*__shortcut;
	/* That is a workaround for hide window */
	bool				__hide_state;
	Database			__db;
	/* Debounces rapid clipboard updates into one snapshot */
	QTimer				*__clipboard_timer;

	/* widgets */
	SearchBar			*__searchbar;
	QPushButton			*__menu_button;
	QListWidget			*__scroll_widget;

	/* system tray entry (bottom-right corner) */
	QSystemTrayIcon			*__tray_icon;

	/* It's copyed from myself, We need save icon */
	QPixmap				__pasteitem_icon;

	/* Use for store current row when searching. Not a QObject, so it can't
	 * be a QPointer: clipboard_later() resets it when it deletes the item. */
	QListWidgetItem			*__current_item = nullptr;
};
#endif // MAINWINDOW_H
