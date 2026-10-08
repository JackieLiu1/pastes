#include <QDebug>
#include <QSocketNotifier>
#include <QTimer>
#include <algorithm>
#include <mutex>
#include <vector>

#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>

#include "platform/shortcut_p.h"
#include "shortcutstate.h"

namespace {
std::mutex grabMutex;
std::atomic<XErrorHandler> previousErrorHandler{nullptr};
thread_local Display *grabDisplay = nullptr;
thread_local int grabError = 0;

int recordGrabError(Display *display, XErrorEvent *error)
{
	if (display == grabDisplay) { grabError = error->error_code; return 0; }
	const auto previous = previousErrorHandler.load();
	return previous ? previous(display, error) : 0;
}

int grabShortcut(Display *display, KeyCode key, const std::vector<unsigned> &modifiers)
{
	/* Xlib error handlers are process-wide. Serialize our registration traps
	 * and forward errors from every other connection to the prior handler. */
	std::lock_guard<std::mutex> guard(grabMutex);
	XSync(display, False);
	grabDisplay = display;
	grabError = 0;
	previousErrorHandler.store(XSetErrorHandler(&recordGrabError));
	for (unsigned mask : modifiers)
		XGrabKey(display, key, mask, DefaultRootWindow(display), False, GrabModeAsync, GrabModeAsync);
	XSync(display, False);
	XSetErrorHandler(previousErrorHandler.load());
	grabDisplay = nullptr;
	return grabError;
}

unsigned numLockModifier(Display *display)
{
	const KeyCode numLock = XKeysymToKeycode(display, XK_Num_Lock);
	std::unique_ptr<XModifierKeymap, decltype(&XFreeModifiermap)> map(
		XGetModifierMapping(display), &XFreeModifiermap);
	unsigned modifiers = 0;
	if (numLock && map) {
		for (int modifier = 0; modifier < 8; ++modifier)
			for (int key = 0; key < map->max_keypermod; ++key)
				if (map->modifiermap[modifier*map->max_keypermod+key] == numLock)
					modifiers |= 1U << modifier;
	}
	return modifiers;
}
}

class ShortcutPrivate::NativeState
{
public:
	explicit NativeState(ShortcutPrivate &worker) : m_worker(worker) {}

	void processEvents(void)
	{
		while (!m_worker.m_stoped.load() && XPending(display.get())) {
			XEvent event{};
			XNextEvent(display.get(), &event);
			if (event.type != KeyPress && event.type != KeyRelease) continue;
			/* Servers without detectable repeat send adjacent release/press
			 * pairs with the same timestamp while the physical key stays down. */
			if (event.type == KeyRelease && event.xkey.keycode == pasteKey && XPending(display.get())) {
				XEvent next{};
				XPeekEvent(display.get(), &next);
				if (next.type == KeyPress && next.xkey.keycode == pasteKey &&
					next.xkey.time == event.xkey.time) continue;
			}
			if (keys.update(event.type, event.xkey.keycode, pasteKey, event.xkey.state, lockModifiers))
				emit m_worker.pasteActivated();
		}
	}

	/* Closing this worker-owned connection also releases all passive grabs. */
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display{nullptr, &XCloseDisplay};
	KeyCode pasteKey = 0;
	unsigned lockModifiers = LockMask;
	LinuxShortcutState keys;

private:
	ShortcutPrivate &m_worker;
};

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->wait();
}

void ShortcutPrivate::run(void)
{
	if (m_stoped.load()) return;
	NativeState state(*this);
	state.display.reset(XOpenDisplay(nullptr));
	if (!state.display) {
		qWarning() << "Pastes: unable to open the X11 shortcut connection";
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	state.pasteKey = XKeysymToKeycode(state.display.get(), XK_v);
	if (!state.pasteKey) {
		qWarning() << "Pastes: unable to resolve the X11 V key";
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	const unsigned numLock = numLockModifier(state.display.get());
	state.lockModifiers |= numLock;
	std::vector<unsigned> modifiers{Mod4Mask, Mod4Mask | LockMask,
		Mod4Mask | numLock, Mod4Mask | LockMask | numLock};
	std::sort(modifiers.begin(), modifiers.end());
	modifiers.erase(std::unique(modifiers.begin(), modifiers.end()), modifiers.end());
	const int error = grabShortcut(state.display.get(), state.pasteKey, modifiers);
	if (error) {
		qWarning() << "Pastes: unable to grab Win+V, X11 error" << error;
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	Bool detectable = False;
	XkbSetDetectableAutoRepeat(state.display.get(), True, &detectable);
	emit primaryShortcutChanged(QStringLiteral("Win+V"));
	QSocketNotifier notifier(ConnectionNumber(state.display.get()), QSocketNotifier::Read);
	connect(&notifier, &QSocketNotifier::activated, &notifier, [&state] { state.processEvents(); });
	QTimer::singleShot(0, &notifier, [this, &state] {
		state.processEvents();
		if (m_stoped.load()) quit();
	});
	if (!m_stoped.load()) exec();
}

void ShortcutPrivate::stop(void)
{
	m_stoped.store(true);
	quit();
}

QString GlobalShortcut::primaryShortcut(void) const
{
	return QStringLiteral("Win+V");
}
