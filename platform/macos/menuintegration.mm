#include "platform/menuintegration.h"

#include <QApplication>
#include <QMenu>
#include <QWidget>
#import <AppKit/AppKit.h>

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

static bool popupNativeMenu(QMenu *menu, QWidget *anchor)
{
	NSView *view = nativeView(anchor->window());
	if (!view)
		return false;
	NSMenu *nativeMenu = menu->toNSMenu();
	if (!nativeMenu)
		return false;
	/* Keep Qt's delegate and action targets. AppKit supplies the rounded
	 * material, selection, spacing and key-equivalent columns. */
	nativeMenu.appearance = [NSAppearance appearanceNamed:
		qApp->property("pastesDark").toBool() ?
		NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	nativeMenu.font = [NSFont menuFontOfSize:0];
	const QPoint point = anchor->mapTo(anchor->window(),
		QPoint(anchor->width(), anchor->height()+4));
	const NSPoint location = NSMakePoint(point.x()-nativeMenu.size.width,
		view.flipped ? point.y() : view.bounds.size.height-point.y());
	[nativeMenu popUpMenuPositioningItem:nil atLocation:location inView:view];
	return true;
}

const Platform::MenuAppearance &Platform::menuAppearance(void)
{
	static const MenuAppearance appearance = [] {
		MenuAppearance value;
		value.showPanelAction = false;
		value.standardShortcuts = true;
		value.preferencesSeparator = true;
		return value;
	}();
	return appearance;
}

void Platform::popupMenu(QMenu *menu, QWidget *anchor)
{
	if (!popupNativeMenu(menu, anchor))
		menu->exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}
