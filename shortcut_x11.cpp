#include <QKeySequence>
#include <QDebug>

#include <X11/Xlib.h>
#include <X11/extensions/record.h>
#include <X11/Xlibint.h>

#include "shortcut_x11.h"

Display		*m_display;
XRecordContext	m_context;

static void callback(XPointer ptr, XRecordInterceptData *data)
{
	if (data->category == XRecordFromServer) {
		xEvent *event = reinterpret_cast<xEvent*>(data->data);
		switch (event->u.u.type) {
		case KeyPress:
			if (static_cast<unsigned char*>(data->data)[1] == 37  /* Left  Control */||
			    static_cast<unsigned char*>(data->data)[1] == 105 /* Right Control */)
				emit reinterpret_cast<ShortcutPrivateX11*>(ptr)->activated();
			break;
		default:
			break;
		}
	}

	XRecordFreeData(data);
}

ShortcutPrivateX11::ShortcutPrivateX11(QObject *parent) : QThread(parent)
{
	this->start();
}

ShortcutPrivateX11::~ShortcutPrivateX11()
{
	this->stop();
	this->deleteLater();
}

void ShortcutPrivateX11::run(void)
{
	Display *display = XOpenDisplay(nullptr);
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

void ShortcutPrivateX11::stop()
{
	XRecordDisableContext(m_display, m_context);
	XFlush(m_display);
	XCloseDisplay(m_display);
}
