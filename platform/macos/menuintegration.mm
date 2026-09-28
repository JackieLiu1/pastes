#include "platform/menuintegration.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QWidget>
#import <AppKit/AppKit.h>

@interface PastesMenuSelection : NSObject {
@public
	QList<QPointer<QAction>> actions;
	QPointer<QAction> chosen;
}
- (void)selectAction:(NSMenuItem *)item;
@end

@implementation PastesMenuSelection
- (void)selectAction:(NSMenuItem *)item
{
	if (item.tag >= 0 && item.tag < actions.size())
		chosen = actions.at(item.tag);
}
@end

static NSMenu *nativeMenu(QMenu *menu, PastesMenuSelection *selection)
{
	NSMenu *result = [[[NSMenu alloc] initWithTitle:menu->title().toNSString()]
		autorelease];
	result.autoenablesItems = NO;
	result.font = [NSFont menuFontOfSize:0];
	result.appearance = [NSAppearance appearanceNamed:
		qApp->property("pastesDark").toBool() ?
		NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	for (QAction *action : menu->actions()) {
		if (!action->isVisible())
			continue;
		if (action->isSeparator()) {
			[result addItem:NSMenuItem.separatorItem];
			continue;
		}
		/* Use Qt's public conversion for native shortcut labels and icons.
		 * Copy only AppKit properties, never Qt's targets or delegates. */
		QMenu converted;
		converted.addAction(action);
		NSMenuItem *source = converted.toNSMenu().itemArray.firstObject;
		if (!source)
			continue;
		NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:source.title
			action:@selector(selectAction:) keyEquivalent:source.keyEquivalent]
			autorelease];
		item.keyEquivalentModifierMask = source.keyEquivalentModifierMask;
		item.image = source.image;
		item.state = source.state;
		item.enabled = action->isEnabled();
		item.target = selection;
		item.tag = selection->actions.size();
		selection->actions.append(action);
		if (QMenu *submenu = action->menu())
			item.submenu = nativeMenu(submenu, selection);
		[result addItem:item];
	}
	return result;
}

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

static void finishMouseTracking(NSView *view)
{
	/* Selection can use a different button from the opening press. Finish
	 * both consumed releases through NSResponder, including Control-click,
	 * without releasing a button that the user is still physically holding. */
	const NSUInteger held = NSEvent.pressedMouseButtons;
	const NSPoint location = [view.window convertPointFromScreen:NSEvent.mouseLocation];
	const NSEventType releases[] = {NSEventTypeLeftMouseUp, NSEventTypeRightMouseUp};
	for (int button = 0; button < 2; ++button) {
		if (held & (1u << button))
			continue;
		NSEvent *release = [NSEvent mouseEventWithType:releases[button]
			location:location modifierFlags:NSEvent.modifierFlags
			timestamp:NSProcessInfo.processInfo.systemUptime
			windowNumber:view.window.windowNumber context:nil
			eventNumber:0 clickCount:1 pressure:0];
		if (button == 0)
			[view mouseUp:release];
		else
			[view rightMouseUp:release];
	}
}

static bool popupNativeMenu(QMenu *menu, QWidget *owner,
			    const QPoint &point, bool alignRight,
			    QPointer<QAction> &chosen)
{
	NSView *view = nativeView(owner->window());
	if (!view)
		return false;
	QPointer<QMenu> menuGuard(menu);
	QPointer<QWidget> windowGuard(owner->window());
	@autoreleasepool {
		[[view retain] autorelease];
		emit menu->aboutToShow();
		if (!menuGuard || !windowGuard)
			return true;
		PastesMenuSelection *selection = [[[PastesMenuSelection alloc] init]
			autorelease];
		NSMenu *popup = nativeMenu(menu, selection);
		const NSPoint location = NSMakePoint(
			point.x()-(alignRight ? popup.size.width : 0),
			view.isFlipped ? point.y() : view.bounds.size.height-point.y());
		[popup popUpMenuPositioningItem:nil atLocation:location inView:view];
		if (windowGuard)
			finishMouseTracking(view);
		if (menuGuard)
			emit menu->aboutToHide();
		if (menuGuard && windowGuard)
			chosen = selection->chosen;
		if (chosen && (!chosen->isEnabled() || !chosen->isVisible()))
			chosen.clear();
	}
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
	const QPoint point = anchor->mapTo(anchor->window(),
		QPoint(anchor->width(), anchor->height()+4));
	QPointer<QAction> chosen;
	if (!popupNativeMenu(menu, anchor, point, true, chosen))
		menu->exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
	else if (chosen)
		chosen->trigger();
}

QAction *Platform::execMenuAt(QMenu *menu, QWidget *owner, const QPoint &position)
{
	QPointer<QAction> chosen;
	const bool native = popupNativeMenu(menu, owner,
		owner->window()->mapFromGlobal(position), false, chosen);
	if (!native)
		return menu->exec(position);
	if (chosen)
		chosen->trigger();
	return chosen.data();
}
