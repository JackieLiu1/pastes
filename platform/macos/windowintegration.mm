#include "platform/windowintegration.h"

#include <QWidget>
#include <QGuiApplication>
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QTimer>
#import <AppKit/AppKit.h>

static QScreen *panelScreen(void)
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
			for (QWidget *window : QApplication::topLevelWidgets()) {
				if (Platform::isOwnedWindow(m_widget, window) &&
					notification.object == nativeView(window).window) {
					this->dismiss(false);
					break;
				}
			}
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
				if (Platform::isOwnedWindow(m_widget, active))
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

void Platform::watchPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss)
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

void Platform::updatePanelBackdrop(QWidget *widget)
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
	widget->setProperty("pastesPanelBackdrop", true);
	widget->update();
}

static void prepareFloatingWindow(QWidget *widget)
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
}

void Platform::preparePanel(QWidget *widget)
{
	prepareFloatingWindow(widget);
	Platform::updatePanelBackdrop(widget);
}

void Platform::preparePreview(QWidget *widget)
{
	prepareFloatingWindow(widget);
	Platform::prepareDialog(widget);
	/* Keep the modal preview above its bottom panel. */
	nativeView(widget).window.level = CGWindowLevelForKey(kCGScreenSaverWindowLevelKey)+1;
}

void Platform::activatePanel(QWidget *widget)
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

void Platform::activatePreview(QWidget *widget)
{
	/* Qt assigns a tool window's level when mapping it. Restore the preview
	 * level after that step, before ordering it above the parent panel. */
	Platform::preparePreview(widget);
	Platform::activatePanel(widget);
}

void Platform::prepareDialog(QWidget *widget)
{
	NSWindow *window = nativeView(widget).window;
	if (!window)
		return;
#if QT_VERSION < QT_VERSION_CHECK(6, 9, 0)
	/* Older Qt lacks the expanded client-area flags. */
	window.styleMask |= NSWindowStyleMaskFullSizeContentView;
#endif
	window.titleVisibility = NSWindowTitleHidden;
	window.titlebarAppearsTransparent = YES;
	window.hasShadow = NO;
	window.opaque = NO;
	window.backgroundColor = [NSColor clearColor];
	window.appearance = [NSAppearance appearanceNamed:
		qApp->property("pastesDark").toBool() ?
		NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
	if (@available(macOS 11.0, *))
		window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
	/* Keep AppKit's close target and Qt's window delegate, so a red-button
	 * close rejects the modal dialog just like Escape. */
	[window standardWindowButton:NSWindowCloseButton].enabled = YES;
	[window standardWindowButton:NSWindowMiniaturizeButton].enabled = NO;
	[window standardWindowButton:NSWindowZoomButton].enabled = NO;
}

namespace Platform {

const PanelAppearance &panelAppearance(void)
{
	static const PanelAppearance appearance = [] {
		PanelAppearance value;
		value.margins = QMargins(24, 12, 24, 12);
		value.spacing = 8;
		value.shadow = false;
		value.nativeBackdrop = true;
		value.hideForDialog = true;
		return value;
	}();
	return appearance;
}

const DialogAppearance &dialogAppearance(void)
{
	static const DialogAppearance appearance = [] {
		DialogAppearance value;
		value.flags = Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
			Qt::WindowCloseButtonHint | Qt::NoDropShadowWindowHint;
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
		value.flags |= Qt::ExpandedClientAreaHint | Qt::NoTitleBarBackgroundHint;
#endif
		value.outerMargins = QMargins(0, 0, 0, 0);
		value.contentMargins = QMargins(24, 44, 24, 24);
		value.nativeControls = true;
		value.scrollBar = Qt::ScrollBarAlwaysOff;
		return value;
	}();
	return appearance;
}

QRect panelGeometry(QScreen *screen)
{
	if (!screen) screen = panelScreen();
	if (!screen) return QRect();
	const QRect area = screen->geometry();
	const int height = qMin(area.height(), 414);
	return QRect(area.x(), area.bottom()-height+1, area.width(), height);
}

QSize cardSize(const QSize &panelSize)
{
	/* Include the card's existing 4 px shadow gutter on each side. */
	const int height = qMax(110, panelSize.height()-126);
	return QSize(qMin(280, qRound(height*17.0/18.0)+8), height+8);
}

void initializePanel(QWidget *widget)
{
	/* A keyable NSPanel can join other applications' full-screen Spaces. */
	widget->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
		Qt::NoDropShadowWindowHint | Qt::Tool);
	widget->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
}

void enablePanelBlur(QWidget *) {}

void initializeDialog(QWidget *widget)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
	widget->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
#else
	Q_UNUSED(widget);
#endif
}

void initializePreview(QWidget *widget)
{
	/* Share dialog chrome while retaining a nonactivating, keyable NSPanel. */
	widget->setWindowFlags((dialogAppearance().flags & ~Qt::WindowType_Mask) | Qt::Tool);
	initializeDialog(widget);
	widget->setAttribute(Qt::WA_MacAlwaysShowToolWindow);
}

}
