#include "ui/mainwindow.h"
#include "ui/historyview.h"
#include "ui/appdialog.h"
#include "ui/settingsdialog.h"
#include "ui/previewdialog.h"
#include "application/clipboardcontroller.h"
#include "platform/globalshortcut.h"
#include "platform/pastetarget.h"
#include "platform/windowintegration.h"
#include "platform/menuintegration.h"

#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QMenu>
#include <QPropertyAnimation>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>

MainWindow::MainWindow(HistoryService &history, ClipboardController &clipboard, QWidget *parent)
	: QMainWindow(parent), __history(history), __clipboard(clipboard)
{
	const QRect geometry = Platform::panelGeometry(QApplication::primaryScreen());
	setFixedSize(geometry.size());
	setGeometry(geometry);
	Platform::initializePanel(this);
	setFocusPolicy(Qt::NoFocus);
	setFont(QFont(QStringLiteral("Segoe UI"), 10));
	setAttribute(Qt::WA_TranslucentBackground, true);
	__main_frame = new HistoryView(history, this);
	setCentralWidget(__main_frame);
	applyTheme(QSettings().value("theme", "light").toString());
	Platform::enablePanelBlur(this);
	__main_frame_shadow = new QGraphicsDropShadowEffect(this);
	__main_frame_shadow->setOffset(0, 0);
	__main_frame_shadow->setColor(QColor(0, 0, 0, 130));
	__main_frame_shadow->setBlurRadius(24);
	if (Platform::panelAppearance().shadow) __main_frame->setGraphicsEffect(__main_frame_shadow);
	__main_frame->setRecordingEnabled(clipboard.recordingEnabled());
	connect(__main_frame, &HistoryView::hideRequested, this, &MainWindow::hide_window);
	connect(__main_frame, &HistoryView::copyRequested, this, &MainWindow::copyEntry);
	connect(__main_frame, &HistoryView::previewRequested, this, &MainWindow::previewEntry);
	connect(__main_frame, &HistoryView::countChanged, this, &MainWindow::updateTrayTooltip);
	connect(&clipboard, &ClipboardController::recordingChanged, this, [this](bool enabled) {
		__main_frame->setRecordingEnabled(enabled);
		updateTrayTooltip();
	});
	connect(&history, &HistoryService::loaded, this, [this](void) {
		/* Warm the native window once, retaining the existing startup behavior. */
		setVisible(true);
		setVisible(false);
	});
	__hide_animation = new QPropertyAnimation(this, "pos", this);
	__hide_animation->setDuration(200);
	__hide_animation->setStartValue(pos());
	__hide_animation->setEndValue(QPoint(geometry.x(), geometry.bottom()+1));
	__hide_animation->setEasingCurve(QEasingCurve::OutQuad);
	connect(__hide_animation, &QPropertyAnimation::finished, this, [this](void) {
		if (__hide_animation->direction() == QAbstractAnimation::Forward) hide();
	});
	__shortcut = new GlobalShortcut(this);
	__primary_shortcut = __shortcut->primaryShortcut();
	connect(__shortcut, &GlobalShortcut::pasteActivated, this, [this](void) {
		if (__hide_state) show_window(); else hide_window();
	});
	connect(__shortcut, &GlobalShortcut::primaryShortcutChanged, this, [this](const QString &shortcut) {
		__primary_shortcut = shortcut;
		updateShortcutHint();
	});
	__paste_target = new PasteTarget(this);
	connect(__paste_target, &PasteTarget::permissionRequired, this, [this](void) {
		PastePermissionDialog dialog(this);
		execAppDialog(dialog);
	}, Qt::QueuedConnection);
	Platform::watchPanelDismissal(this, [this](bool immediate) {
		if (immediate) {
			__paste_target->cancel();
			__hide_animation->stop();
			__hide_state = true;
			hide();
		} else hide_window();
	});
	setupTrayIcon();
	updateShortcutHint();
}

bool MainWindow::event(QEvent *event)
{
	if (event->type() == QEvent::ActivationChange) {
		QTimer::singleShot(0, this, [this](void) {
			if (!Platform::isOwnedWindow(this, QApplication::activeWindow())) hide_window();
		});
	}
	return QMainWindow::event(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
	__main_frame->focusCurrent();
	QMainWindow::showEvent(event);
}

void MainWindow::hideEvent(QHideEvent *event)
{
	__main_frame->cancelInteractions();
	QMainWindow::hideEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
	QMainWindow::resizeEvent(event);
	Platform::updatePanelBackdrop(this);
}

void MainWindow::show_window(void)
{
	if (__app_dialog_open && Platform::panelAppearance().hideForDialog) return;
	if (__hide_state) __paste_target->captureTarget(this);
	__clipboard.flushPending();
	const QRect geometry = Platform::panelGeometry();
	setFixedSize(geometry.size());
	setGeometry(geometry);
	Platform::preparePanel(this);
	__hide_animation->setStartValue(pos());
	__hide_animation->setEndValue(QPoint(geometry.x(), geometry.bottom()+1));
	__hide_animation->setDirection(QAbstractAnimation::Backward);
	__hide_animation->start();
	__hide_state = false;
	show();
	Platform::activatePanel(this);
}

void MainWindow::hide_window(void)
{
	if (__main_frame) __main_frame->cancelInteractions();
	if (__hide_state) return;
	__hide_animation->setDirection(QAbstractAnimation::Forward);
	__hide_animation->setStartValue(pos());
	__hide_animation->setEndValue(QPoint(x(), y()+height()));
	__hide_animation->start();
	__hide_state = true;
}

void MainWindow::copyEntry(HistoryEntry entry, bool plainText, bool paste)
{
	if (paste) hide_window();
	if (__clipboard.copy(*entry, plainText) && paste)
		__paste_target->paste(this, entry->mimeData->hasUrls());
}

void MainWindow::previewEntry(HistoryEntry entry)
{
	const HistoryEntry snapshot = cloneEntry(*entry);
	PreviewDialog dialog(*snapshot, this);
	connect(&dialog, &PreviewDialog::copyRequested, this, [this, snapshot](void) {
		__clipboard.copy(*snapshot);
	});
	if (dialog.exec() == QDialog::Accepted && (!dialog.plainText() || snapshot->mimeData->hasText())) {
		copyEntry(snapshot, dialog.plainText(), true);
	} else if (isVisible()) {
		Platform::activatePanel(this);
		__main_frame->focusEntry(snapshot->md5);
	}
}

void MainWindow::showSettings(void)
{
	SettingsDialog dialog(__primary_shortcut, this);
	connect(&dialog, &SettingsDialog::themeChanged, this, &MainWindow::applyTheme);
	connect(&dialog, &SettingsDialog::recordingChanged, &__clipboard, &ClipboardController::setRecordingEnabled);
	connect(&dialog, &SettingsDialog::hintsChanged, __main_frame, &HistoryView::setKeyboardHintsVisible);
	connect(__shortcut, &GlobalShortcut::primaryShortcutChanged, &dialog, &SettingsDialog::setPrimaryShortcut);
	execAppDialog(dialog);
}

void MainWindow::updateTrayTooltip(void)
{
	if (!__tray_icon) return;
	__tray_icon->setToolTip(QString("Pastes · %1\n%2 ").arg(__primary_shortcut)
		.arg(__history.entries().size()) + QObject::tr("records") +
		(__clipboard.recordingEnabled() ? QString() : '\n'+QObject::tr("Recording paused")));
}

void MainWindow::updateShortcutHint(void)
{
	__main_frame->setPrimaryShortcut(__primary_shortcut);
	if (__show_action) __show_action->setText(QObject::tr("Show (%1)").arg(__primary_shortcut));
	updateTrayTooltip();
}

void MainWindow::setupTrayIcon(void)
{
	QMenu *tray_menu = new QMenu(this);
	QMenu *panel_menu = new QMenu(this->__main_frame->menuAnchor());

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
	QObject::connect(this->__main_frame, &HistoryView::menuRequested, this, [this, panel_menu](void) {
		Platform::popupMenu(panel_menu, this->__main_frame->menuAnchor());
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

void MainWindow::applyTheme(const QString &name)
{
	if (this->__main_frame) this->__main_frame->cancelInteractions();
	qApp->setProperty("pastesDark", name != "light");
	QString file = (name == "light") ? ":/resources/theme-light.qss"
					 : ":/resources/theme-dark.qss";

	QFile qss(file);
	if (qss.open(QFile::ReadOnly)) {
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

void MainWindow::execAppDialog(AppDialog &dialog)
{
	/* Cover the activation events dispatched by the modal event loop,
	 * including those emitted before Qt registers the active dialog. */
	QScopedValueRollback<bool> dialogOpen(this->__app_dialog_open, true);
	if (Platform::panelAppearance().hideForDialog)
		this->hide_window();
	dialog.exec();
}
