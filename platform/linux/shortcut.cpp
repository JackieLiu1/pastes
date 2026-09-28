#include <QKeySequence>
#include <QDebug>

#include <X11/Xlib.h>
#include <X11/extensions/record.h>
#include <X11/Xlibint.h>
#include <X11/keysym.h>

#include "platform/shortcut_p.h"

static Display		*m_display;
static XRecordContext	m_context;
static KeyCode m_paste_keycode;

static void callback(XPointer ptr, XRecordInterceptData *data)
{
	if (data->category == XRecordFromServer) {
		xEvent *event = reinterpret_cast<xEvent*>(data->data);
		switch (event->u.u.type) {
		case KeyPress:
			if (event->u.u.detail == m_paste_keycode &&
			    (event->u.keyButtonPointer.state & (ControlMask | ShiftMask)) ==
			    (ControlMask | ShiftMask)) {
				emit reinterpret_cast<ShortcutPrivate*>(ptr)->pasteActivated();
				break;
			}
			break;
		default:
			break;
		}
	}

	XRecordFreeData(data);
}

class ShortcutPrivate::NativeState {};

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->deleteLater();
}

void ShortcutPrivate::run(void)
{
	emit this->primaryShortcutChanged(QStringLiteral("Ctrl+Shift+V"));
	Display *display = XOpenDisplay(nullptr);
	m_paste_keycode = XKeysymToKeycode(display, XK_v);
	XRecordClientSpec clients = XRecordAllClients;
	XRecordRange *range = XRecordAllocRange();

	memset(range, 0, sizeof(XRecordRange));
	range->device_events.first = KeyPress;
	range->device_events.last = KeyPress;

	m_context = XRecordCreateContext(display, 0, &clients, 1, &range, 1);
	XFree(range);
	XSync(display, True);

	m_display = XOpenDisplay(nullptr);
	XRecordEnableContext(m_display, m_context, &callback, reinterpret_cast<XPointer>(this));
}

void ShortcutPrivate::stop(void)
{
	XRecordDisableContext(m_display, m_context);
	XFlush(m_display);
	XCloseDisplay(m_display);
}

QString GlobalShortcut::primaryShortcut(void) const
{
	return QStringLiteral("Ctrl+Shift+V");
}
