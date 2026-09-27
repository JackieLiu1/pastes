#include "window_mac.h"

#include <QWidget>
#include <QGuiApplication>
#include <QApplication>
#import <AppKit/AppKit.h>

void configureMacApplication(void)
{
	if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

static NSImage *panelMask(void)
{
	/* Equal caps keep AppKit's stretch region in the opaque center,
	 * rather than stretching the top arcs down the side edges. */
	static NSImage *mask = [[NSImage imageWithSize:NSMakeSize(38, 38)
		flipped:NO drawingHandler:^BOOL(NSRect) {
		NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:
			NSMakeRect(0, 0, 38, 38) xRadius:18 yRadius:18];
		[[NSColor whiteColor] setFill];
		[path fill];
		/* Fill the lower corners so the panel reaches the screen bottom. */
		NSRectFill(NSMakeRect(0, 0, 38, 20));
		return YES;
	}] retain];
	mask.capInsets = NSEdgeInsetsMake(18, 18, 18, 18);
	mask.resizingMode = NSImageResizingModeStretch;
	return mask;
}

void updateMacPanelBackdrop(QWidget *widget)
{
	NSView *view = nativeView(widget);
	NSView *parent = view.superview;
	if (!parent)
		return;
	NSVisualEffectView *backdrop = nil;
	for (NSView *child in parent.subviews) {
		if ([child.identifier isEqualToString:@"PastesPanelBackdrop"] &&
			[child isKindOfClass:[NSVisualEffectView class]]) {
			backdrop = static_cast<NSVisualEffectView *>(child);
			break;
		}
	}
	if (!backdrop) {
		backdrop = [[NSVisualEffectView alloc] initWithFrame:view.frame];
		backdrop.identifier = @"PastesPanelBackdrop";
		backdrop.material = NSVisualEffectMaterialHUDWindow;
		backdrop.blendingMode = NSVisualEffectBlendingModeBehindWindow;
		backdrop.state = NSVisualEffectStateActive;
		backdrop.maskImage = panelMask();
		backdrop.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
		/* Keep Qt's content view and responder intact. A sibling below it
		 * supplies the blur without covering the cards or taking input. */
		[parent addSubview:backdrop positioned:NSWindowBelow relativeTo:view];
		[backdrop release];
	}
	backdrop.frame = view.frame;
	backdrop.appearance = [NSAppearance appearanceNamed:
		qApp->property("pastesDark").toBool() ?
		NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	widget->setProperty("macPanelBackdrop", true);
	widget->update();
}

void prepareMacPanel(QWidget *widget)
{
	/* Create the native window only when preparing to show it. */
	widget->winId();
	NSWindow *window = nativeView(widget).window;
	if (!window)
		return;
	/* A nonactivating NSPanel can take keyboard focus in another app's
	 * full-screen Space without activating Pastes' desktop Space. */
	if ([window isKindOfClass:[NSPanel class]]) {
		if (!(window.styleMask & NSWindowStyleMaskNonactivatingPanel))
			window.styleMask |= NSWindowStyleMaskNonactivatingPanel;
		NSPanel *panel = static_cast<NSPanel *>(window);
		panel.floatingPanel = YES;
		panel.becomesKeyOnlyIfNeeded = NO;
	}
	window.hidesOnDeactivate = NO;
	window.hasShadow = NO;
	window.opaque = NO;
	window.backgroundColor = [NSColor clearColor];
	NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
		NSWindowCollectionBehaviorFullScreenAuxiliary |
		NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle;
	if (@available(macOS 13.0, *))
		behavior |= NSWindowCollectionBehaviorCanJoinAllApplications;
	window.collectionBehavior = behavior;
	/* Stay above full-screen content as well as the Dock. */
	window.level = CGWindowLevelForKey(kCGScreenSaverWindowLevelKey);
	updateMacPanelBackdrop(widget);
}

void activateMacPanel(QWidget *widget)
{
	NSView *view = nativeView(widget);
	NSWindow *window = view.window;
	if (!window)
		return;
	/* Raising through Qt activates NSApp and can switch desktop Spaces. */
	[window orderFrontRegardless];
	[window makeKeyWindow];
	[window makeFirstResponder:view];
}
