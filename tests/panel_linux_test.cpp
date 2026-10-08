#include "tests/testsupport.h"
#include "application/clipboardcontroller.h"
#include "ui/mainwindow.h"
#include "ui/historyview.h"
#include "ui/previewdialog.h"
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

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

class NativeInput final
{
public:
	NativeInput(void) : display(XOpenDisplay(nullptr), &XCloseDisplay)
	{
		if (!display) return;
		int event = 0, error = 0, major = 0, minor = 0;
		if (!XTestQueryExtension(display.get(), &event, &error, &major, &minor)) return;
		Window root = DefaultRootWindow(display.get()), returnedRoot = 0, child = 0;
		int x = 0, y = 0;
		unsigned mask = 0;
		XQueryPointer(display.get(), root, &returnedRoot, &child, &pointerX, &pointerY, &x, &y, &mask);
		char keys[32]{};
		XQueryKeymap(display.get(), keys);
		if (mask & (Button1Mask | Button2Mask | Button3Mask)) return;
		for (char key : keys) if (key) return;
		XGetInputFocus(display.get(), &previous, &revert);
		XSetWindowAttributes attributes{};
		attributes.override_redirect = True;
		outside = XCreateWindow(display.get(), root, 40, 40, 300, 160, 0,
			CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
		XSelectInput(display.get(), outside, ButtonPressMask);
		XMapRaised(display.get(), outside);
		focusOutside();
	}
	~NativeInput(void)
	{
		if (!outside) return;
		if (m_pressedButton) XTestFakeButtonEvent(display.get(), m_pressedButton, False, CurrentTime);
		XDestroyWindow(display.get(), outside);
		XTestFakeMotionEvent(display.get(), -1, pointerX, pointerY, CurrentTime);
		XSetInputFocus(display.get(), previous, revert, CurrentTime);
		XSync(display.get(), False);
	}
	Window focus(void)
	{
		Window window = 0;
		int policy = 0;
		XGetInputFocus(display.get(), &window, &policy);
		return window;
	}
	void focusOutside(void)
	{
		XSetInputFocus(display.get(), outside, RevertToPointerRoot, CurrentTime);
		XSync(display.get(), False);
	}
	void key(QWidget *receiver, KeySym symbol)
	{
		require(focus() == receiver->winId(), "Refusing to send a test key to another window");
		const KeyCode code = XKeysymToKeycode(display.get(), symbol);
		require(code != 0, "Test key has no native mapping");
		XTestFakeKeyEvent(display.get(), code, True, CurrentTime);
		XTestFakeKeyEvent(display.get(), code, False, CurrentTime);
		XFlush(display.get());
	}
	void mouse(const QPoint &position, int button = 0, bool pressed = false)
	{
		XTestFakeMotionEvent(display.get(), -1, position.x(), position.y(), CurrentTime);
		if (button) {
			XTestFakeButtonEvent(display.get(), button, pressed, CurrentTime);
			m_pressedButton = pressed ? button : 0;
		}
		XFlush(display.get());
	}
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display;
	Window outside = 0;
private:
	Window previous = 0;
	int revert = 0, pointerX = 0, pointerY = 0;
	int m_pressedButton = 0;
};

void nativePanelLifecycle(void)
{
	if (QGuiApplication::platformName() != "xcb") {
		qInfo() << "SKIP native panel lifecycle: requires QT_QPA_PLATFORM=xcb";
		return;
	}
	NativeInput input;
	if (!input.outside) {
		qInfo() << "SKIP native panel lifecycle: no XTest display or input is busy";
		return;
	}
	MemoryRepository repository;
	HistoryService history(repository);
	IdleFeed feed;
	ClipboardController clipboard(history, feed, *QApplication::clipboard(), false);
	MainWindow panel(history, clipboard);
	history.load();
	repository.finishLoad({textEntry("Native test one"), textEntry("Native test two")});
	auto *view = panel.findChild<HistoryView *>();
	auto *list = view->findChild<QListWidget *>();
	for (int iteration = 0; iteration < 6; ++iteration) {
		panel.show_window();
		waitUntil([&] { return panel.isVisible() && input.focus() == panel.winId() && panel.isActiveWindow(); });
		const int before = list->currentRow();
		input.key(&panel, before == 0 ? XK_Right : XK_Left);
		waitUntil([&] { return list->currentRow() != before; });
		if (iteration % 2) {
			input.key(&panel, XK_Escape);
		} else {
			/* This independent X11 window receives the click without taking
			 * focus. A QWidget ActivationChange cannot dismiss the panel. */
			input.mouse(QPoint(120, 100), 1, true);
			input.mouse(QPoint(120, 100), 1, false);
		}
		waitUntil([&] { return !panel.isVisible(); });
		input.focusOutside();
	}
	panel.show_window();
	panel.hide_window();
	panel.show_window(); // Reverse an in-flight hide before native unmap.
	waitUntil([&] { return panel.isVisible() && input.focus() == panel.winId() && panel.isActiveWindow(); });
	QMenu menu(&panel);
	menu.addAction("Native test menu");
	menu.popup(panel.mapToGlobal(QPoint(200, 100)));
	waitUntil([&] { return menu.isVisible() && QApplication::activePopupWidget() == &menu; });
	input.key(&panel, XK_Escape); // Qt routes this native key to its popup.
	waitUntil([&] { return !menu.isVisible(); });
	require(panel.isVisible(), "Closing an owned menu dismissed the panel");
	menu.popup(panel.mapToGlobal(QPoint(200, 100)));
	waitUntil([&] { return menu.isVisible() && QApplication::activePopupWidget() == &menu; });
	input.mouse(QPoint(120, 100), 1, true);
	input.mouse(QPoint(120, 100), 1, false);
	waitUntil([&] { return !menu.isVisible() && !panel.isVisible(); });
	panel.show_window();
	waitUntil([&] { return panel.isVisible() && input.focus() == panel.winId() && panel.isActiveWindow(); });
	bool inspected = false, dragged = false, closed = false;
	QTimer preview;
	preview.setInterval(10);
	QObject::connect(&preview, &QTimer::timeout, &panel, [&] {
		auto *dialog = qobject_cast<PreviewDialog *>(QApplication::activeModalWidget());
		if (!dialog || input.focus() != dialog->winId() || !dialog->isActiveWindow()) return;
		preview.stop();
		inspected = panel.isVisible();
		auto *title = dialog->findChild<QLabel *>("PreviewTitle");
		if (!title) { dialog->reject(); return; }
		auto *header = title->parentWidget();
		const QPoint start = dialog->pos();
		const QPoint press = header->mapToGlobal(header->rect().center());
		input.mouse(press, 1, true);
		QTimer::singleShot(50, dialog, [&, dialog, start, press] {
			input.mouse(press + QPoint(60, -30));
			QTimer::singleShot(50, dialog, [&, dialog, start, press] {
				input.mouse(press + QPoint(60, -30), 1, false);
				QTimer::singleShot(50, dialog, [&, dialog, start] {
					dragged = dialog->pos() == start + QPoint(60, -30);
					input.key(dialog, XK_Escape);
					closed = true;
				});
			});
		});
	});
	preview.start();
	QTimer::singleShot(2500, &panel, [] {
		if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
	});
	input.key(&panel, XK_space);
	waitUntil([&] { return inspected && closed && !QApplication::activeModalWidget(); });
	require(dragged, "Native preview header drag did not move the window");
	require(panel.isVisible(), "Preview interaction dismissed its owner");
	waitUntil([&] { return input.focus() == panel.winId() && panel.isActiveWindow(); });
	bool outsidePreviewClosed = false;
	QTimer outsidePreview;
	outsidePreview.setInterval(10);
	QObject::connect(&outsidePreview, &QTimer::timeout, &panel, [&] {
		auto *dialog = qobject_cast<PreviewDialog *>(QApplication::activeModalWidget());
		if (!dialog || input.focus() != dialog->winId()) return;
		outsidePreview.stop();
		input.mouse(QPoint(120, 100), 1, true);
		input.mouse(QPoint(120, 100), 1, false);
		outsidePreviewClosed = true;
	});
	outsidePreview.start();
	input.key(&panel, XK_space);
	waitUntil([&] { return outsidePreviewClosed && !panel.isVisible() && !QApplication::activeModalWidget(); });
	panel.show_window();
	waitUntil([&] { return panel.isVisible() && input.focus() == panel.winId() && panel.isActiveWindow(); });
	input.focusOutside();
	waitUntil([&] { return !panel.isVisible(); });
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	QCoreApplication::setOrganizationName("PastesValidation");
	QCoreApplication::setApplicationName("NativePanelTests");
	QTemporaryDir settings;
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
	return runTest("native panel input, outside dismissal and preview drag", nativePanelLifecycle);
}
