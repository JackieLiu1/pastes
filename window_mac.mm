#include "window_mac.h"

#include <QWidget>
#include <QGuiApplication>
#import <AppKit/AppKit.h>

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

void prepareMacPanel(QWidget *widget)
{
	/* Create the native window only when preparing to show it. */
	widget->winId();
	NSWindow *window = nativeView(widget).window;
	if (!window)
		return;
	window.hidesOnDeactivate = NO;
	window.hasShadow = NO;
	window.opaque = NO;
	window.backgroundColor = [NSColor clearColor];
	/* Cover the Dock at the screen edge. */
	window.level = CGWindowLevelForKey(kCGScreenSaverWindowLevelKey);
}
