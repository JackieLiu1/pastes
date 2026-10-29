#include "platform/pastetarget.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <xcb/xcb.h>

Q_LOGGING_CATEGORY(pasteLog, "pastes.paste", QtWarningMsg)

namespace {
template<typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
template<typename T> Reply<T> reply(T *value) { return Reply<T>(value, &std::free); }

bool sendPasteShortcut(Display *display)
{
	const KeyCode insert = XKeysymToKeycode(display, XK_Insert);
	const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
	if (!insert || !shift) return false;
	XTestFakeKeyEvent(display, shift, True, CurrentTime);
	XTestFakeKeyEvent(display, insert, True, CurrentTime);
	XTestFakeKeyEvent(display, insert, False, CurrentTime);
	XTestFakeKeyEvent(display, shift, False, CurrentTime);
	XFlush(display);
	return true;
}
}

class PasteTarget::Private
{
public:
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display{nullptr, &XCloseDisplay};
	std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection{nullptr, &xcb_disconnect};
	xcb_window_t target = XCB_NONE;
	xcb_window_t root = XCB_NONE;
	QPointer<QWidget> panel;
	QTimer timer;
	QElapsedTimer elapsed;
	bool activationRequested = false;
};

PasteTarget::PasteTarget(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>())
{
	m_private->timer.setInterval(20);
	QObject::connect(&m_private->timer, &QTimer::timeout, this, &PasteTarget::tryPaste);
}

PasteTarget::~PasteTarget() = default;

void PasteTarget::captureTarget(QWidget *panel)
{
	Q_UNUSED(panel);
	cancel();
	m_private->target = XCB_NONE;
	/* XTest cannot paste into native Wayland clients or offscreen widgets. */
	if (QGuiApplication::platformName() != QStringLiteral("xcb")) return;
	if (!m_private->display) {
		m_private->display.reset(XOpenDisplay(nullptr));
		if (!m_private->display) return;
		int event = 0, error = 0, major = 0, minor = 0;
		if (!XTestQueryExtension(m_private->display.get(), &event, &error, &major, &minor)) {
			m_private->display.reset();
			return;
		}
	}
	if (!m_private->connection || xcb_connection_has_error(m_private->connection.get()))
		m_private->connection.reset(xcb_connect(nullptr, nullptr));
	auto *connection = m_private->connection.get();
	if (xcb_connection_has_error(connection)) return;
	auto focus = reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
	if (!focus || focus->focus <= XCB_INPUT_FOCUS_POINTER_ROOT || QWidget::find(focus->focus)) return;
	auto tree = reply(xcb_query_tree_reply(connection, xcb_query_tree(connection, focus->focus), nullptr));
	if (!tree || focus->focus == tree->root) return;
	m_private->target = focus->focus;
	m_private->root = tree->root;
}

void PasteTarget::paste(QWidget *panel, bool hasUrls)
{
	cancel();
	if (!panel || hasUrls || !m_private->target) return;
	m_private->panel = panel;
	m_private->activationRequested = false;
	m_private->elapsed.start();
	m_private->timer.start();
}

void PasteTarget::tryPaste(void)
{
	auto *connection = m_private->connection.get();
	if (!m_private->panel || !connection || xcb_connection_has_error(connection)) {
		cancel();
		return;
	}
	if (m_private->elapsed.elapsed() > 1500) {
		qCWarning(pasteLog) << "Timed out waiting for paste target focus or input release";
		cancel();
		return;
	}
	if (m_private->panel->isVisible()) return;
	/* Checked replies contain a disappearing target's BadWindow locally. */
	auto attributes = reply(xcb_get_window_attributes_reply(connection,
		xcb_get_window_attributes(connection, m_private->target), nullptr));
	if (!attributes || attributes->map_state != XCB_MAP_STATE_VIEWABLE) {
		cancel();
		return;
	}
	auto focus = reply(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
	if (!focus) { cancel(); return; }
	if (focus->focus != m_private->target && focus->focus > XCB_INPUT_FOCUS_POINTER_ROOT &&
		focus->focus != m_private->root && !QWidget::find(focus->focus)) {
		cancel(); // Respect an application switch while a paste is pending.
		return;
	}
	/* Wait for physical Enter, quick-paste modifiers and mouse buttons.
	 * Injecting while they are held can change the target's paste command. */
	auto keys = reply(xcb_query_keymap_reply(connection, xcb_query_keymap(connection), nullptr));
	auto pointer = reply(xcb_query_pointer_reply(connection,
		xcb_query_pointer(connection, m_private->root), nullptr));
	if (!keys || !pointer) { cancel(); return; }
	for (uint8_t key : keys->keys) if (key) return;
	if (pointer->mask & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) return;
	if (focus->focus != m_private->target) {
		if (!m_private->activationRequested) {
			m_private->activationRequested = true;
			auto error = reply(xcb_request_check(connection, xcb_set_input_focus_checked(connection,
				XCB_INPUT_FOCUS_POINTER_ROOT, m_private->target, XCB_CURRENT_TIME)));
			if (error) cancel();
		}
		return; // Confirm the server's focus on the next tick before injection.
	}
	cancel();
	if (!sendPasteShortcut(m_private->display.get()))
		qCWarning(pasteLog) << "No native mapping for the paste shortcut";
}

void PasteTarget::cancel(void)
{
	m_private->timer.stop();
	m_private->panel.clear();
}

void PasteTarget::requestPermission(void) {}
QString PasteTarget::permissionRecoveryHint(void) { return {}; }
