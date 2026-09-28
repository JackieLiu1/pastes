#include "platform/clipboardsource.h"

#include <QApplication>
#include <QClipboard>
#include <QPixmap>
#include <QDebug>
#include <vector>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

class ClipboardSource::Private {};

ClipboardSource::ClipboardSource(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>())
{
	QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged,
		this, &ClipboardSource::clipboardChanged);
}

ClipboardSource::~ClipboardSource() = default;

int ClipboardSource::settleInterval(void) const
{
	return 1000;
}

bool ClipboardSource::synchronize(void)
{
	return false;
}

void ClipboardSource::capture(quint64 request)
{
	Q_UNUSED(request);
	/* Retain the X11 lookup at snapshot time, as before. */
}

static bool get_window_name2(Display* dpy, Window window, char* buf)
{
	XTextProperty tp;

	XGetTextProperty(dpy, window, &tp, XInternAtom(dpy, "WM_NAME", False));
	if (tp.nitems > 0) {
		int count = 0, i, ret;
		char **list = NULL;

		ret = XmbTextPropertyToTextList(dpy, &tp, &list, &count);
		if((ret == Success || ret > 0) && list != NULL){
			for(i=0; i<count; i++)
				snprintf(buf, 1024, "%s", list[i]);
			XFreeStringList(list);
		} else {
			snprintf(buf, 1024, "%s", tp.value);
		}

		return true;
	} else {
		return false;
	}
}

static QString strip_cmd(QString window_title)
{
	if (window_title.contains("Qt Selection Owner")) {
		return window_title.mid(23);
	} else if (window_title.contains("Chromium ")) {
		return "chrome";
	}

	return window_title;
}

QImage ClipboardSource::snapshotIcon(void)
{
	QPixmap pixmap;

	int i = 0;
	Display *display = XOpenDisplay(NULL);
	Atom clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
	Window clipboard_owner_win = XGetSelectionOwner(display, clipboard_atom);
	char buf[1024] = {0};
	unsigned long nitems, bytesafter;
	unsigned char *ret;
	int format;
	Atom type;
	Atom wm_icon_atom = XInternAtom(display, "_NET_WM_ICON", True);
	qDebug() << clipboard_owner_win;
	/* Get clipboard owner title name */
	get_window_name2(display, clipboard_owner_win, buf);
	QString command = strip_cmd(buf);
	qDebug() << buf << command;

	/* Search from [-100, 100] */
	clipboard_owner_win -= 100;
again:
	/* Get the width of the icon */
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   0, 1, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		/* FIXME: In fact, Get clipboard window id from XLIB is not the
		 * actual window id, but it is strange that his actual ID is
		 * near this, between -100 and +100.
		 *
		 * I didn't find out what happened, but he seems to be working.
		 * if anyone finds a good way, please let me know.
		 */
		clipboard_owner_win++;
		if (i++ > 200) {
			XCloseDisplay(display);
			qDebug() << "Not found icon, Use default Linux logo";
			pixmap.convertFromImage(QImage(":/resources/ubuntu.png"));
			return pixmap.scaled(32, 32, Qt::KeepAspectRatio,
		Qt::SmoothTransformation).toImage();
		}

		goto again;
	}

	int width = *(int *)ret;
	XFree(ret);

	/* Get the height of the Icon */
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   1, 1, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		qDebug() << "No X11 Icon height Found.";
		return pixmap.scaled(32, 32, Qt::KeepAspectRatio,
		Qt::SmoothTransformation).toImage();
	}

	int height = *(int *)ret;
	XFree(ret);

	/* Get data from Icon */
	int size = width * height;
	XGetWindowProperty(display,
			   clipboard_owner_win,
			   wm_icon_atom,
			   2, size, 0,
			   XA_CARDINAL,
			   &type,
			   &format,
			   &nitems,
			   &bytesafter,
			   &ret);
	if (!ret) {
		qDebug() << "No X11 Icon Data Found.";
		return pixmap.scaled(32, 32, Qt::KeepAspectRatio,
		Qt::SmoothTransformation).toImage();
	}

	unsigned long *imgArr = (unsigned long*)(ret);
	std::vector<uint32_t> imgARGB32(size);
	for(int i=0; i<size; ++i)
		imgARGB32[i] = (uint32_t)(imgArr[i]);

	QImage *image = new QImage((uchar*)imgARGB32.data(), width, height, QImage::Format_ARGB32);
	pixmap.convertFromImage(*image);

	XFree(ret);
	delete image;
	XCloseDisplay(display);

	return pixmap.scaled(32, 32, Qt::KeepAspectRatio,
		Qt::SmoothTransformation).toImage();
}
