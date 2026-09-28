#include "platform/pastetarget.h"

#include <QTimer>
#include <X11/Xlib.h>
#include <X11/Intrinsic.h>
#include <X11/extensions/XTest.h>

namespace {
static void SendKey(Display * disp, KeySym keysym, KeySym modsym)
{
	KeyCode keycode = 0, modcode = 0;
	keycode = XKeysymToKeycode (disp, keysym);
	if (keycode == 0)
		return;

	XTestGrabControl (disp, True);
	/* Generate modkey press */
	if (modsym != 0) {
		modcode = XKeysymToKeycode(disp, modsym);
		XTestFakeKeyEvent (disp, modcode, True, 0);
	}
	/* Generate regular key press and release */
	XTestFakeKeyEvent (disp, keycode, True, 0);
	XTestFakeKeyEvent (disp, keycode, False, 0);

	/* Generate modkey release */
	if (modsym != 0)
		XTestFakeKeyEvent (disp, modcode, False, 0);

	XSync (disp, False);
	XTestGrabControl (disp, False);
}
}

class PasteTarget::Private {};
PasteTarget::PasteTarget(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>()) {}
PasteTarget::~PasteTarget() = default;
void PasteTarget::captureTarget(QWidget *panel) { Q_UNUSED(panel); }
void PasteTarget::cancel(void) {}
void PasteTarget::requestPermission(void) {}

void PasteTarget::paste(QWidget *panel, bool hasUrls)
{
	Q_UNUSED(panel);
	if (hasUrls) return;
	QTimer::singleShot(1000, this, [](void) {
		Display *display = XOpenDisplay(nullptr);
		if (!display) return;
		SendKey(display, XK_Insert, XK_Shift_L);
		XCloseDisplay(display);
	});
}
