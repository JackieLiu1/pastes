#include "tests/testsupport.h"
#include "platform/globalshortcut.h"
#include <QCoreApplication>
#include <QDir>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include "platform/linux/shortcutstate.h"

namespace {

class DisplayEnvironment final
{
public:
	DisplayEnvironment(void) : m_present(qEnvironmentVariableIsSet("DISPLAY")), m_value(qgetenv("DISPLAY"))
	{
		qputenv("DISPLAY", QByteArray());
	}
	~DisplayEnvironment(void)
	{
		if (m_present) qputenv("DISPLAY", m_value);
		else qunsetenv("DISPLAY");
	}
private:
	bool m_present;
	QByteArray m_value;
};

int openDescriptors(void)
{
	return QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
}

void winPasteChord(void)
{
	constexpr unsigned pasteKey = 55;
	require(LinuxShortcutState::matches(KeyPress, pasteKey, pasteKey, Mod4Mask),
		"Win+V did not activate history");
	require(LinuxShortcutState::matches(KeyPress, pasteKey, pasteKey, Mod4Mask | LockMask | Mod2Mask),
		"Caps Lock or Num Lock blocked Win+V");
	for (unsigned modifiers : {0U, unsigned(ControlMask | ShiftMask),
		unsigned(Mod4Mask | ControlMask), unsigned(Mod4Mask | ShiftMask), unsigned(Mod4Mask | Mod1Mask)})
		require(!LinuxShortcutState::matches(KeyPress, pasteKey, pasteKey, modifiers),
			"Another modifier combination activated history");
	require(!LinuxShortcutState::matches(KeyRelease, pasteKey, pasteKey, Mod4Mask) &&
		!LinuxShortcutState::matches(KeyPress, pasteKey+1, pasteKey, Mod4Mask) &&
		!LinuxShortcutState::matches(KeyPress, 0, 0, Mod4Mask),
		"A release, unrelated key or missing V mapping activated history");
	require(LinuxShortcutState::matches(KeyPress, pasteKey, pasteKey, Mod4Mask | Mod3Mask,
		LockMask | Mod3Mask), "A remapped Num Lock blocked Win+V");
	LinuxShortcutState state;
	require(!state.update(KeyPress, pasteKey, pasteKey, Mod4Mask), "History opened while the keyboard was grabbed");
	for (int i = 0; i < 20; ++i)
		require(!state.update(KeyPress, pasteKey, pasteKey, Mod4Mask), "Holding V repeated activation");
	state.update(KeyRelease, pasteKey+1, pasteKey, Mod4Mask);
	require(!state.update(KeyPress, pasteKey, pasteKey, Mod4Mask), "Another key release reset held V");
	require(state.update(KeyRelease, pasteKey, pasteKey, 0), "First Win+V release was lost");
	require(!state.update(KeyRelease, pasteKey, pasteKey, 0), "A duplicate release activated history");
	require(!state.update(KeyPress, pasteKey, pasteKey, Mod4Mask), "Second press opened during the grab");
	require(state.update(KeyRelease, pasteKey, pasteKey, Mod4Mask), "A second Win+V release was lost");
}

void missingDisplayAndEarlyStop(void)
{
	DisplayEnvironment environment;
	const int before = openDescriptors();
	{
		GlobalShortcut shortcut;
		require(shortcut.primaryShortcut() == "Win+V", "Default Linux shortcut hint is not Win+V");
		QString primary;
		QObject::connect(&shortcut, &GlobalShortcut::primaryShortcutChanged, &shortcut,
			[&](const QString &value) { primary = value; });
		waitUntil([&] { return !primary.isEmpty(); });
		require(primary == QObject::tr("Tray icon"), "Missing X11 display did not fall back to the tray");
	}
	for (int i = 0; i < 50; ++i) {
		GlobalShortcut shortcut;
	}
	require(openDescriptors() == before, "Failed initialization or early shutdown leaked descriptors");
}

void grabLifecycle(void)
{
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), &XCloseDisplay);
	if (!display) {
		qInfo() << "SKIP X11 grab lifecycle: no display";
		return;
	}
	const int before = openDescriptors();
	/* A conflicting instance falls back without removing the first grab. */
	for (int i = 0; i < 10; ++i) {
		GlobalShortcut first;
		QString firstPrimary, secondPrimary;
		QObject::connect(&first, &GlobalShortcut::primaryShortcutChanged, &first,
			[&](const QString &value) { firstPrimary = value; });
		waitUntil([&] { return !firstPrimary.isEmpty(); });
		if (i == 0 && firstPrimary != "Win+V") {
			qInfo() << "SKIP X11 grab lifecycle: Win+V is already occupied";
			return;
		}
		require(firstPrimary == "Win+V", "Closing a shortcut did not release its X11 grab");
		GlobalShortcut second;
		QObject::connect(&second, &GlobalShortcut::primaryShortcutChanged, &second,
			[&](const QString &value) { secondPrimary = value; });
		waitUntil([&] { return !secondPrimary.isEmpty(); });
		require(secondPrimary == QObject::tr("Tray icon"), "Conflicting grab did not fall back to the tray");
	}
	require(openDescriptors() == before, "Repeated X11 grab shutdown leaked display connections");
}

void shortcutDoesNotTypeV(void)
{
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), &XCloseDisplay);
	int eventBase = 0, errorBase = 0, major = 0, minor = 0;
	if (!display || !XTestQueryExtension(display.get(), &eventBase, &errorBase, &major, &minor)) {
		qInfo() << "SKIP native Win+V input delivery: no display with XTest support";
		return;
	}
	const KeyCode super = XKeysymToKeycode(display.get(), XK_Super_L);
	const KeyCode paste = XKeysymToKeycode(display.get(), XK_v);
	require(super && paste, "Native shortcut key mappings missing");
	GlobalShortcut shortcut;
	QString primary;
	int activations = 0;
	QObject::connect(&shortcut, &GlobalShortcut::primaryShortcutChanged, &shortcut,
		[&](const QString &value) { primary = value; });
	QObject::connect(&shortcut, &GlobalShortcut::pasteActivated, &shortcut, [&] { ++activations; });
	waitUntil([&] { return !primary.isEmpty(); });
	if (primary != "Win+V") {
		qInfo() << "SKIP native Win+V input delivery: shortcut already occupied";
		return;
	}
	struct InputWindow {
		Display *display;
		Window window = 0, previous = 0;
		int revert = 0;
		explicit InputWindow(Display *connection) : display(connection)
		{
			XGetInputFocus(display, &previous, &revert);
			XSetWindowAttributes attributes{};
			attributes.override_redirect = True;
			window = XCreateWindow(display, DefaultRootWindow(display), 0, 0, 100, 60,
				0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
			XSelectInput(display, window, KeyPressMask | KeyReleaseMask);
			XMapWindow(display, window);
			XSetInputFocus(display, window, RevertToParent, CurrentTime);
			XSync(display, False);
		}
		~InputWindow(void)
		{
			XSetInputFocus(display, previous, revert, CurrentTime);
			XDestroyWindow(display, window);
			XSync(display, False);
		}
	} target(display.get());
	XTestFakeKeyEvent(display.get(), super, True, CurrentTime);
	for (int i = 0; i < 3; ++i) XTestFakeKeyEvent(display.get(), paste, True, CurrentTime);
	XTestFakeKeyEvent(display.get(), paste, False, CurrentTime);
	XTestFakeKeyEvent(display.get(), super, False, CurrentTime);
	XSync(display.get(), False);
	waitUntil([&] { return activations >= 1; });
	require(activations == 1, "Repeated Win+V toggled the panel more than once");
	auto typedKeys = [&] {
		int typed = 0;
		while (XPending(display.get())) {
			XEvent event{};
			XNextEvent(display.get(), &event);
			if (event.type == KeyPress && event.xkey.keycode == paste) ++typed;
		}
		return typed;
	};
	require(typedKeys() == 0, "Win+V also typed V into the original focused window");
	XTestFakeKeyEvent(display.get(), paste, True, CurrentTime);
	XTestFakeKeyEvent(display.get(), paste, False, CurrentTime);
	XSync(display.get(), False);
	require(typedKeys() == 1, "Shortcut capture consumed ordinary V typing");
}

}

int main(int argc, char **argv)
{
	XInitThreads();
	QCoreApplication app(argc, argv);
	return runTest("Linux Win+V chord and lock modifiers", winPasteChord) |
		runTest("missing X11 display and immediate shortcut destruction", missingDisplayAndEarlyStop) |
		runTest("X11 shortcut conflicts and repeated grab shutdown", grabLifecycle) |
		runTest("Win+V consumes V while ordinary typing remains available", shortcutDoesNotTypeV);
}
