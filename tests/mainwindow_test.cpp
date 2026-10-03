#include "tests/testsupport.h"
#include "tests/colorcontrast.h"
#include "application/clipboardcontroller.h"
#include "ui/mainwindow.h"
#include "ui/historyview.h"
#include "ui/previewdialog.h"
#include "ui/appdialog.h"
#include "ui/settingsdialog.h"
#include "ui/roundedwidgets.h"
#include "platform/windowintegration.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>

namespace {
class IdleFeed final : public ClipboardFeed
{
public:
	int settleInterval(void) const override { return 0; }
	void capture(quint64) override {}
	bool synchronize(void) override { return false; }
	bool allowsCapture(void) const override { return false; }
	QImage snapshotIcon(void) override { return {}; }
};

struct WindowFixture
{
	MemoryRepository repository;
	HistoryService history{repository};
	IdleFeed feed;
	ClipboardController clipboard{history, feed, *QApplication::clipboard(), false};
	MainWindow window{history, clipboard};
};

void trayPanelActionFollowsVisibility(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	auto *action = window.findChild<QAction *>("ShowHistoryAction");
	require(action, "Tray history action missing");
	const QString showLabel = action->text();
	action->trigger();
	require(window.isVisible(), "Tray action did not open history");
	require(action->text() != showLabel, "Visible history still offers a show action");
	action->trigger();
	require(action->text() == showLabel, "Hide animation did not update the tray action");
	action->trigger(); // Reverse the pending hide before QWidget becomes hidden.
	require(window.isVisible() && action->text() != showLabel,
		"Reopening during hide left the tray action stale");
	window.toggle_window(); // The same entry point used by the global shortcut.
	waitUntil([&] { return !window.isVisible(); });
	require(action->text() == showLabel, "Shortcut dismissal left the tray action stale");
	window.show_window();
	window.hide(); // Secondary instances and immediate platform dismissal.
	require(action->text() == showLabel, "Direct dismissal left a hide action");
	window.toggle_window();
	require(window.isVisible() && action->text() != showLabel,
		"Shortcut toggle could not reopen after direct dismissal");
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
}

void backdropUsesOpaqueFallback(void)
{
	QWidget panel;
	panel.resize(160, 100);
	RoundedSurface surface;
	for (bool dark : {false, true}) {
		qApp->setProperty("pastesDark", dark);
		for (bool active : {false, true, false}) {
			panel.setProperty("pastesPanelBackdrop", active);
			QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			QPainter painter(&image);
			surface.paint(&panel, RoundedRole::Panel, painter);
			painter.end();
			const int alpha = image.pixelColor(80, 50).alpha();
			if (Platform::panelAppearance().nativeBackdrop) {
				if (active) require(alpha > 0 && alpha < 255, "Active backdrop is covered by opaque paint");
				else require(alpha == 255, "Unavailable backdrop left a translucent panel");
			}
			require(image.pixelColor(3, 3).alpha() == 0 &&
				image.pixelColor(156, 3).alpha() == 0 &&
				image.pixelColor(15, 2).alpha() > 0 &&
				image.pixelColor(144, 2).alpha() > 0,
				"Panel did not retain its 18 DIP custom top corners");
			require(image.pixelColor(2, 97).alpha() > 0 &&
				image.pixelColor(157, 97).alpha() > 0,
				"Panel rounded a bottom corner");
			QPainter cardPainter(&image);
			surface.paint(&panel, RoundedRole::Card, cardPainter);
			cardPainter.end();
			require(image.pixelColor(80, 50).alpha() == 255, "Glass made card contents translucent");
		}
	}
	qApp->setProperty("pastesDark", false);
}

void appDialogsHideHistory(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	for (const char *actionName : {"SettingsAction", "AboutAction"}) {
		auto *action = window.findChild<QAction *>(QString::fromLatin1(actionName));
		require(action, "Application dialog action missing");
		for (bool fromPanel : {true, false}) {
			if (fromPanel) window.show_window();
			bool inspected = false, hidden = false, reopeningBlocked = false;
			QTimer inspection;
			inspection.setSingleShot(true);
			QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || !dialog->isVisible()) return;
				inspected = true;
				hidden = !window.isVisible();
				window.show_window(); // Shared tray and global-shortcut entry point.
				reopeningBlocked = !window.isVisible();
				if (auto *close = dialog->findChild<QPushButton *>("PreviewClose")) close->click();
				else dialog->reject();
			});
			inspection.start(350); // Wait for the panel's 200 ms hide animation.
			QTimer watchdog;
			watchdog.setSingleShot(true);
			QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
				if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					dialog->reject();
			});
			watchdog.start(2000);
			action->trigger();
			require(inspected, "Application dialog did not stay open");
			require(hidden, "Application dialog left the history panel visible");
			require(reopeningBlocked, "History reopened over an application dialog");
			require(!window.isVisible(), "Closing an application dialog reopened history");
		}
		window.show_window();
		require(window.isVisible(), "History could not reopen after closing the dialog");
		window.hide_window();
		waitUntil([&] { return !window.isVisible(); });
	}
}

void lightThemeTextContrast(void)
{
	QSettings().setValue("theme", "light");
	WindowFixture fixture;
	fixture.history.load();
	fixture.repository.finishLoad({textEntry("Readable clipboard text")});
	auto &window = fixture.window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.show_window();
	auto *view = window.findChild<HistoryView *>();
	require(view, "History view missing");
	view->ensurePolished();
	RoundedSurface surface;
	for (bool active : {false, true}) {
		window.setProperty("pastesPanelBackdrop", active);
		for (const QColor &background : {QColor(Qt::black), QColor(Qt::white)}) {
			QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(background);
			QPainter painter(&image);
			surface.paint(view, RoundedRole::Panel, painter);
			painter.end();
			const QColor panelColor = image.pixelColor(view->width()/2, view->height()/2);
			for (const char *name : {"BrandTitle", "HistoryTab", "FavoritesTab", "HistoryCount", "KeyboardHint",
				"EmptyState", "RecordingStatus", "UndoHint", "UndoButton", "PanelMenu"}) {
				auto *label = view->findChild<QWidget *>(name);
				require(label, (std::string("Panel text widget missing: ")+name).c_str());
				label->ensurePolished();
				const QColor ink = label->palette().color(QPalette::WindowText);
				require(contrastRatio(ink, panelColor) >= 4.5,
					(std::string("Light panel text lost contrast: ")+name).c_str());
			}
		}
	}
	auto *card = view->findChild<QWidget *>("PasteItemFrame");
	require(card, "Contrast test did not bind the history card");
	QImage image(card->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	surface.paint(card, RoundedRole::Card, painter);
	painter.end();
	const QColor cardColor = image.pixelColor(card->width()/2, card->height()/2);
	for (QLabel *label : card->findChildren<QLabel *>()) {
		if (label->text().isEmpty()) continue;
		label->ensurePolished();
		require(contrastRatio(label->palette().color(QPalette::WindowText), cardColor) >= 4.5,
			(std::string("Light card metadata lost contrast: ")+label->objectName().toStdString()).c_str());
	}
}

void favoriteNameDialogCommands(void)
{
	WindowFixture fixture; auto item = textEntry("unchanged payload");
	fixture.history.load(); fixture.repository.finishLoad({item});
	fixture.history.setFavorite(item->id, true);
	fixture.history.setFavoriteName(item->id, "original");
	QEventLoop startup; QTimer::singleShot(50, &startup, &QEventLoop::quit); startup.exec();
	auto &window = fixture.window; auto *view = window.findChild<HistoryView *>();
	for (int mode : {0, 1, 2}) {
		window.show_window();
		bool inspected = false, blocked = false, validInput = false;
		QTimer inspection;
		QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
			auto *dialog = dynamic_cast<FavoriteNameDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			inspected = true;
			auto *edit = dialog->findChild<QLineEdit *>("FavoriteNameEdit");
			validInput = edit && edit->maxLength() == FavoriteDetails::maxNameLength;
			window.toggle_window(); blocked = window.isVisible();
			if (edit) edit->setText(mode == 2 ? "" : "edited name");
			if (mode == 0) dialog->reject(); else dialog->accept();
		});
		inspection.start(20);
		QTimer::singleShot(1500, &window, [] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
		});
		view->renameFavoriteRequested(item);
		inspection.stop();
		require(inspected, "Name dialog did not open");
		require(validInput, "Name dialog lost its input length limit");
		require(blocked, "Panel toggle was not blocked while naming");
		require(window.isVisible(), "Name dialog did not preserve the visible panel");
		require(item->favoriteDetails.name == (mode == 0 ? "original" : mode == 1 ? "edited name" : ""),
			"Name dialog save, cancel or clear command failed");
		require(item->mimeData->text() == "unchanged payload", "Name dialog modified clipboard content");
	}
	window.hide();
}

void previewKeepsHistoryVisible(void)
{
	WindowFixture fixture;
	auto entry = textEntry("preview with visible history");
	fixture.history.load();
	fixture.repository.finishLoad({entry});
	QEventLoop startup;
	QTimer::singleShot(50, &startup, &QEventLoop::quit);
	startup.exec();
	auto &window = fixture.window;
	auto *view = window.findChild<HistoryView *>();
	require(view, "History view missing");
	for (bool escape : {false, true}) {
		window.show_window();
		bool inspected = false, panelVisible = false;
		QTimer inspection;
		inspection.setSingleShot(true);
		QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
			auto *dialog = qobject_cast<PreviewDialog *>(QApplication::activeModalWidget());
			if (!dialog || !dialog->isVisible()) return;
			inspected = true;
			panelVisible = window.isVisible();
			if (escape) {
				QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
				QApplication::sendEvent(dialog, &key);
			} else if (auto *close = dialog->findChild<QPushButton *>("PreviewClose")) close->click();
			else dialog->reject();
		});
		inspection.start(350);
		QTimer watchdog;
		watchdog.setSingleShot(true);
		QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				dialog->reject();
		});
		watchdog.start(2000);
		emit view->previewRequested(entry);
		require(inspected && panelVisible, "Preview hid its history panel");
		require(window.isVisible(), "Closing preview hid its history panel");
	}
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
}

void dialogGlassContrast(bool dark)
{
	QFile theme(dark ? ":/resources/theme-dark.qss" : ":/resources/theme-light.qss");
	require(theme.open(QIODevice::ReadOnly), "Dialog contrast test could not load the theme");
	qApp->setProperty("pastesDark", dark);
	auto entry = textEntry("Readable preview text");
	std::unique_ptr<QDialog> dialogs[] = {
		std::make_unique<AboutDialog>(), std::make_unique<SettingsDialog>("Win+V"),
		std::make_unique<PreviewDialog>(*entry)};
	const QString stylesheet = QString::fromUtf8(theme.readAll());
	for (auto &dialog : dialogs) {
		dialog->setAttribute(Qt::WA_DontShowOnScreen);
		dialog->setStyleSheet(stylesheet);
		dialog->show();
		auto *surface = dialog->findChild<QWidget *>("AppDialogSurface");
		if (!surface) surface = dialog->findChild<QWidget *>("PreviewSurface");
		require(surface, "Dialog surface missing");
		RoundedSurface paint;
		for (bool active : {false, true, false}) {
			dialog->setProperty("pastesDialogBackdrop", active);
			QImage tint(surface->size(), QImage::Format_ARGB32_Premultiplied);
			tint.fill(Qt::transparent);
			QPainter painter(&tint);
			paint.paint(surface, RoundedRole::Preview, painter);
			painter.end();
			const QColor center = tint.pixelColor(tint.width()/2, tint.height()/2);
			require(active ? center.alpha() < 192 : center.alpha() == 255,
				"Dialog did not switch between visible glass and opaque fallback");
			for (int x : {3, tint.width()-4})
				for (int y : {3, tint.height()-4})
					require(tint.pixelColor(x, y).alpha() == 0, "Dialog lost a custom corner");
			for (const QColor &background : {QColor(Qt::black), QColor(Qt::white)}) {
				QImage composite(tint.size(), tint.format()); composite.fill(background);
				QPainter blend(&composite); blend.drawImage(QPoint(), tint); blend.end();
				const QColor glass = composite.pixelColor(tint.width()/2, tint.height()/2);
				for (QLabel *label : dialog->findChildren<QLabel *>()) {
					if (label->text().isEmpty() || !label->isVisibleTo(dialog.get())) continue;
					bool opaque = label->objectName() == "AboutVersion";
					for (QWidget *parent = label->parentWidget(); parent && parent != surface; parent = parent->parentWidget())
						if (parent->objectName() == "AppDialogCard" || parent->objectName() == "PreviewContent") opaque = true;
					if (opaque) continue;
					label->ensurePolished();
					require(contrastRatio(label->palette().color(QPalette::WindowText), glass) >= 4.5,
						(std::string("Dialog glass text lost contrast: ")+label->objectName().toStdString()).c_str());
				}
			}
		}
		dialog->hide();
	}
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	QTemporaryDir preferences;
	QCoreApplication::setOrganizationName("PastesWindowTests");
	QCoreApplication::setApplicationName("Dialogs");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
	int failures = runTest("tray history action follows visibility and pending animations", trayPanelActionFollowsVisibility);
	failures += runTest("backdrop activation and fallback preserve readable surfaces", backdropUsesOpaqueFallback);
	failures += runTest("light theme text remains readable over black and white backgrounds", lightThemeTextContrast);
	failures += runTest("application dialogs hide history and block reopening", appDialogsHideHistory);
	failures += runTest("preview keeps history visible through close and Escape", previewKeepsHistoryVisible);
	failures += runTest("favorite name dialog saves, clears, cancels and blocks panel toggles", favoriteNameDialogCommands);
	failures += runTest("dialog glass keeps readable text and opaque fallback", [] {
		dialogGlassContrast(false); dialogGlassContrast(true);
	});
	return failures ? 1 : 0;
}
