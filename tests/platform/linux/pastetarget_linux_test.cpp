#include "tests/support/testsupport.h"
#include "tests/core/entryfixtures.h"
#include "application/clipboardcontroller.h"
#include "platform/clipboardsource.h"
#include "platform/pastetarget.h"
#include "storage/database.h"
#include "ui/window/mainwindow.h"
#include "ui/cards/pasteitem.h"
#include <QApplication>
#include <QClipboard>
#include <QElapsedTimer>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProcess>
#include <QTextCursor>
#include <QTemporaryDir>
#include <QSettings>
#include <QWidget>
#include <cstdio>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

namespace {
void waitFor(int milliseconds)
{
	QEventLoop loop;
	QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
	loop.exec();
}

int receiver(QApplication &app)
{
	QPlainTextEdit editor;
	editor.setPlainText("left|right");
	auto cursor = editor.textCursor();
	cursor.setPosition(5);
	editor.setTextCursor(cursor);
	QObject::connect(&editor, &QPlainTextEdit::textChanged, &editor, [&] {
		std::printf("text:%s\n", editor.toPlainText().toUtf8().toBase64().constData());
		std::fflush(stdout);
	});
	editor.show();
	std::printf("%llu\n", static_cast<unsigned long long>(editor.winId()));
	std::fflush(stdout);
	return app.exec();
}

class Receiver final
{
public:
	Receiver(void)
	{
		process.start(QCoreApplication::applicationFilePath(), {"--receiver"});
		require(process.waitForStarted(), "Paste receiver did not start");
		waitUntil([&] { return process.canReadLine(); });
		window = Window(process.readLine().trimmed().toULongLong());
		require(window, "Paste receiver has no native window");
	}
	~Receiver(void) { close(); }
	void close(void)
	{
		if (process.state() == QProcess::NotRunning) return;
		process.terminate();
		if (!process.waitForFinished(1000)) { process.kill(); process.waitForFinished(); }
	}
	QString text(void)
	{
		while (process.canReadLine()) {
			const auto line = process.readLine().trimmed();
			if (line.startsWith("text:")) m_text = QString::fromUtf8(QByteArray::fromBase64(line.mid(5)));
		}
		return m_text;
	}
	QProcess process;
	Window window = 0;
private:
	QString m_text = "left|right";
};

class NativeInput final
{
public:
	NativeInput(void) : display(XOpenDisplay(nullptr), &XCloseDisplay)
	{
		require(bool(display), "Isolated X11 display missing");
		int event = 0, error = 0, major = 0, minor = 0;
		require(XTestQueryExtension(display.get(), &event, &error, &major, &minor), "XTest missing");
	}
	void focus(Window window)
	{
		XSetInputFocus(display.get(), window, RevertToPointerRoot, CurrentTime);
		XSync(display.get(), False);
	}
	Window focused(void)
	{
		Window window = 0;
		int revert = 0;
		XGetInputFocus(display.get(), &window, &revert);
		return window;
	}
	void key(KeySym symbol, bool pressed)
	{
		const auto code = XKeysymToKeycode(display.get(), symbol);
		require(code, "Test key has no native mapping");
		XTestFakeKeyEvent(display.get(), code, pressed, CurrentTime);
		XSync(display.get(), False);
	}
	void button(bool pressed)
	{
		XTestFakeButtonEvent(display.get(), 1, pressed, CurrentTime);
		XSync(display.get(), False);
	}
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display;
};

void pasteWaitsForHideAndRestoresFocus(void)
{
	NativeInput input;
	Receiver target;
	QWidget panel;
	PasteTarget paste;
	input.focus(target.window);
	paste.captureTarget(&panel);
	panel.show();
	input.focus(panel.winId());
	QApplication::clipboard()->setText("native paste");
	paste.paste(&panel, false);
	waitFor(100);
	require(target.text() == "left|right", "Paste reached the target before the panel hid");
	QElapsedTimer elapsed;
	elapsed.start();
	panel.hide();
	waitUntil([&] { return target.text() == "left|native pasteright"; }, 800);
	qInfo() << "Native paste completed after hide in" << elapsed.elapsed() << "ms";
	waitFor(100);
	require(target.text() == "left|native pasteright", "Paste shortcut was delivered twice");
}

void pasteWaitsForPhysicalInput(void)
{
	for (bool mouse : {false, true}) {
		NativeInput input;
		Receiver target;
		QWidget panel;
		PasteTarget paste;
		input.focus(target.window);
		paste.captureTarget(&panel);
		panel.show();
		input.focus(panel.winId());
		QApplication::clipboard()->setText("released");
		if (mouse) input.button(true);
		else input.key(XK_Control_L, true);
		paste.paste(&panel, false);
		panel.hide();
		waitFor(100);
		const bool waited = target.text() == "left|right";
		if (mouse) input.button(false);
		else input.key(XK_Control_L, false);
		require(waited, "Paste did not wait for physical input release");
		waitUntil([&] { return target.text() == "left|releasedright"; }, 800);
	}
}

void cancelledPastesStayCancelled(void)
{
	for (int mode = 0; mode < 4; ++mode) {
		NativeInput input;
		Receiver target;
		Receiver other;
		QWidget panel;
		PasteTarget paste;
		input.focus(target.window);
		paste.captureTarget(&panel);
		panel.show();
		input.focus(panel.winId());
		QApplication::clipboard()->setText("must not paste");
		paste.paste(&panel, false);
		if (mode == 0) paste.cancel();
		if (mode == 1) paste.captureTarget(&panel); // Reopening cancels the old request.
		if (mode == 2) input.focus(other.window);
		if (mode == 3) target.close();
		panel.hide();
		waitFor(1100);
		require(target.text() == "left|right" && other.text() == "left|right",
			"A cancelled, switched or destroyed target received a stale paste");
	}
}

void replacedRequestPastesOnce(void)
{
	NativeInput input;
	Receiver target;
	QWidget panel;
	PasteTarget paste;
	input.focus(target.window);
	paste.captureTarget(&panel);
	panel.show();
	input.focus(panel.winId());
	QApplication::clipboard()->setText("latest");
	paste.paste(&panel, false);
	paste.paste(&panel, false);
	panel.hide();
	waitUntil([&] { return target.text() == "left|latestright"; }, 800);
	waitFor(1100);
	require(target.text() == "left|latestright", "Replacing a paste request duplicated input");
}

void historyPasteSurvivesReload(void)
{
	NativeInput input;
	Receiver target;
	QTemporaryDir data;
	require(data.isValid(), "Temporary history directory missing");
	const QString path = data.filePath("PastesDatabase.db");
	{
		Database repository(path);
		HistoryService history(repository);
		ClipboardSource source;
		ClipboardController clipboard(history, source, *QApplication::clipboard(), true);
		MainWindow panel(history, clipboard);
		bool loaded = false;
		QObject::connect(&history, &HistoryService::loaded, &panel, [&] { loaded = true; });
		history.load();
		waitUntil([&] { return loaded; });
		history.record(textEntry("older history paste"));
		history.record(textEntry("newest"));
		input.focus(target.window);
		panel.show_window();
		waitUntil([&] { return panel.isVisible() && input.focused() == panel.winId(); });
		auto *list = panel.findChild<QListWidget *>();
		list->setCurrentRow(1);
		auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()));
		require(card && card->entry()->mimeData->text() == "older history paste", "Old history card missing");
		bool captured = false, visibleOnCapture = false;
		QObject::connect(&history, &HistoryService::entryAdded, &panel,
			[&](HistoryEntry, int, HistoryChange change) {
				if (change == HistoryChange::Captured) {
					captured = true;
					visibleOnCapture |= panel.isVisible();
				}
			});
		QElapsedTimer elapsed;
		elapsed.start();
		card->copyData();
		require(!panel.isVisible(), "History paste left the panel visible");
		waitUntil([&] { return target.text() == "left|older history pasteright"; }, 800);
		qInfo() << "History selection reached the external editor in" << elapsed.elapsed() << "ms";
		waitUntil([&] { return captured; });
		require(!visibleOnCapture && history.entries().size() == 2 &&
			history.entries().first()->mimeData->text() == "older history paste",
			"Clipboard recapture exposed a visible reorder or duplicated history");
	}
	Database repository(path);
	HistoryService history(repository);
	bool loaded = false;
	QObject::connect(&history, &HistoryService::loaded, &history, [&] { loaded = true; });
	history.load();
	waitUntil([&] { return loaded; });
	require(history.entries().size() == 2 &&
		history.entries().first()->mimeData->text() == "older history paste",
		"Recopied history did not survive database reload");
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	if (app.arguments().contains("--receiver")) return receiver(app);
	if (!app.arguments().contains("--isolated-x11") || QGuiApplication::platformName() != "xcb") {
		qCritical() << "Run this test only on an isolated X11 server with --isolated-x11";
		return 1;
	}
	QTemporaryDir settings;
	QCoreApplication::setOrganizationName("PastesValidation");
	QCoreApplication::setApplicationName("NativePasteTests");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
	int failures = runTest("native paste waits for hide, restores focus and skips the fixed delay", pasteWaitsForHideAndRestoresFocus);
	failures += runTest("native paste waits for modifier and mouse release", pasteWaitsForPhysicalInput);
	failures += runTest("cancel, reopening, application switch and target exit prevent stale paste", cancelledPastesStayCancelled);
	failures += runTest("replacing a pending paste sends one shortcut", replacedRequestPastesOnce);
	failures += runTest("history paste hides before recapture and survives SQLite reload", historyPasteSurvivesReload);
	return failures ? 1 : 0;
}
