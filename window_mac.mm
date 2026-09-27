#include "window_mac.h"

#include <QWidget>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QTimer>
#include <QMenu>
#import <AppKit/AppKit.h>

void configureMacApplication(void)
{
	if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

QScreen *macPanelScreen(void)
{
	/* Pick the frontmost application's display before taking key focus.
	 * Window bounds are available without requesting accessibility access. */
	const pid_t owner = NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier;
	CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly |
		kCGWindowListExcludeDesktopElements, kCGNullWindowID);
	QScreen *screen = nullptr;
	if (windows) {
		for (NSDictionary *info in (NSArray *)windows) {
			if ([info[(id)kCGWindowOwnerPID] intValue] != owner ||
				[info[(id)kCGWindowLayer] intValue] != 0)
				continue;
			CGRect bounds;
			if (!CGRectMakeWithDictionaryRepresentation(
				(CFDictionaryRef)info[(id)kCGWindowBounds], &bounds))
				continue;
			const QRect rect(qRound(bounds.origin.x), qRound(bounds.origin.y),
				qRound(bounds.size.width), qRound(bounds.size.height));
			qint64 largestArea = 0;
			for (QScreen *candidate : QGuiApplication::screens()) {
				const QRect overlap = candidate->geometry().intersected(rect);
				const qint64 area = qint64(overlap.width()) * overlap.height();
				if (area > largestArea) {
					largestArea = area;
					screen = candidate;
				}
			}
			if (screen)
				break;
		}
		CFRelease(windows);
	}
	if (!screen)
		screen = QGuiApplication::screenAt(QCursor::pos());
	return screen ? screen : QGuiApplication::primaryScreen();
}

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

class MacPanelObserver : public QObject
{
public:
	MacPanelObserver(QWidget *widget, const std::function<void(bool)> &dismiss)
		: QObject(widget), m_widget(widget), m_dismiss(dismiss)
	{
		NSNotificationCenter *workspace = NSWorkspace.sharedWorkspace.notificationCenter;
		m_spaceObserver = [workspace addObserverForName:NSWorkspaceActiveSpaceDidChangeNotification
			object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *) {
			/* The old Space is already gone; finish hiding before redisplay. */
			this->dismiss(true);
		}];
		m_applicationObserver = [workspace addObserverForName:NSWorkspaceDidActivateApplicationNotification
			object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
			NSRunningApplication *app = notification.userInfo[NSWorkspaceApplicationKey];
			if (app.processIdentifier != NSProcessInfo.processInfo.processIdentifier)
				this->dismiss(false);
		}];
		m_focusObserver = [NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidResignKeyNotification
			object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
			if (notification.object == nativeView(m_widget).window)
				this->dismiss(false);
		}];
	}

	~MacPanelObserver() override
	{
		NSNotificationCenter *workspace = NSWorkspace.sharedWorkspace.notificationCenter;
		[workspace removeObserver:m_spaceObserver];
		[workspace removeObserver:m_applicationObserver];
		[NSNotificationCenter.defaultCenter removeObserver:m_focusObserver];
	}

private:
	void dismiss(bool immediate)
	{
		/* Let AppKit finish assigning focus before inspecting owned dialogs. */
		QTimer::singleShot(0, this, [this, immediate](void) {
			if (!m_widget->isVisible())
				return;
			if (!immediate) {
				if (nativeView(m_widget).window.keyWindow)
					return;
				QWidget *active = QApplication::activeWindow();
				if (active && active != m_widget && m_widget->isAncestorOf(active))
					return;
			}
			m_dismiss(immediate);
		});
	}

	QWidget *m_widget;
	std::function<void(bool)> m_dismiss;
	id m_spaceObserver;
	id m_applicationObserver;
	id m_focusObserver;
};

void watchMacPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss)
{
	if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
		new MacPanelObserver(widget, dismiss);
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
	/* Move here when summoned, then remain in this Space during a swipe. */
	NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorMoveToActiveSpace |
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

bool popupMacMenu(QMenu *menu, QWidget *anchor)
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
