#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "core/itemdata.h"
#include <QMainWindow>

class HistoryService;
class SyncService;
class ClipboardController;
class HistoryView;
class GlobalShortcut;
class PasteTarget;
class AppDialog;
class QPropertyAnimation;
class QGraphicsDropShadowEffect;
class QSystemTrayIcon;
class QAction;

/* Window shell. Services are assembled at the application entry point;
 * this class coordinates native focus, menus, dialogs and presentation. */
class MainWindow final : public QMainWindow
{
	Q_OBJECT
public:
	MainWindow(HistoryService &history, ClipboardController &clipboard, QWidget *parent = nullptr, SyncService *sync = nullptr);

public slots:
	void show_window(void);
	void hide_window(void);

protected:
	bool event(QEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	void setupTrayIcon(void);
	void updateTrayTooltip(void);
	void updateShortcutHint(void);
	void applyTheme(const QString &name);
	void showSettings(void);
	void execAppDialog(AppDialog &dialog);
	void copyEntry(HistoryEntry entry, bool plainText, bool paste);
	void previewEntry(HistoryEntry entry);
	SyncService *__sync = nullptr;
	HistoryService &__history;
	ClipboardController &__clipboard;
	HistoryView *__main_frame = nullptr;
	QGraphicsDropShadowEffect *__main_frame_shadow = nullptr;
	QPropertyAnimation *__hide_animation = nullptr;
	GlobalShortcut *__shortcut = nullptr;
	PasteTarget *__paste_target = nullptr;
	QSystemTrayIcon *__tray_icon = nullptr;
	QAction *__show_action = nullptr;
	QString __primary_shortcut;
	QString __theme;
	bool __hide_state = true;
	bool __app_dialog_open = false;
};

#endif
