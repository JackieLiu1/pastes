#include "tests/testsupport.h"
#include "application/clipboardcontroller.h"
#include "ui/mainwindow.h"
#include "ui/historyview.h"
#include "ui/previewdialog.h"
#include "ui/roundedwidgets.h"
#include "platform/windowintegration.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

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
	int failures = runTest("backdrop activation and fallback preserve readable surfaces", backdropUsesOpaqueFallback);
	failures += runTest("application dialogs hide history and block reopening", appDialogsHideHistory);
	failures += runTest("preview keeps history visible through close and Escape", previewKeepsHistoryVisible);
	return failures ? 1 : 0;
}
