#include "tests/testsupport.h"
#include "platform/applicationintegration.h"
#include "platform/trayicon.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#import <AppKit/AppKit.h>

/* A menu-bar gesture on macOS 27 can arrive with a non-mouse currentEvent.
 * Override that public property in this test application to reproduce the
 * exact menu-tracking notification which used to crash Qt's tray backend. */
@interface PastesTrayTestApplication : NSApplication
@property(retain) NSEvent *testCurrentEvent;
@end

@implementation PastesTrayTestApplication
- (NSEvent *)currentEvent
{
	return _testCurrentEvent ? _testCurrentEvent : [super currentEvent];
}
- (void)dealloc
{
	[_testCurrentEvent release];
	[super dealloc];
}
@end

namespace {

void repeatedMenuTracking(bool legacy)
{
	QMenu menu;
	auto *action = menu.addAction(QStringLiteral("Tray action"));
	int triggered = 0;
	QObject::connect(action, &QAction::triggered, [&] { ++triggered; });
	TrayIcon tray;
	QSystemTrayIcon legacyTray;
	const QIcon icon = Platform::trayIcon();
	if (legacy) {
		legacyTray.setIcon(icon);
		legacyTray.setContextMenu(&menu);
		legacyTray.show();
	} else {
		tray.setIcon(icon);
		tray.setToolTip(QStringLiteral("Pastes tray contract"));
		tray.setContextMenu(&menu);
		tray.show();
		tray.show(); // Repeated initialization must not duplicate the item.
	}
	NSMenu *native = menu.toNSMenu();
	require(native, "Native tray menu missing");
	auto *application = static_cast<PastesTrayTestApplication *>(NSApp);
	application.testCurrentEvent = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
		location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:0 context:nil
		subtype:0 data1:0 data2:0];
	NSString *failure = nil;
	@try {
		for (int i = 0; i < 100; ++i) {
			[NSNotificationCenter.defaultCenter
				postNotificationName:NSMenuDidBeginTrackingNotification object:native];
			[NSNotificationCenter.defaultCenter
				postNotificationName:NSMenuDidEndTrackingNotification object:native];
		}
		[native performActionForItemAtIndex:0];
	} @catch (NSException *exception) {
		failure = [[exception.reason copy] autorelease];
	}
	application.testCurrentEvent = nil;
	if (failure)
		throw std::runtime_error(failure.UTF8String);
	waitUntil([&] { return triggered == 1; });
	require(triggered == 1, "Tray menu action was lost or repeated");
	if (!legacy) {
		for (int i = 0; i < 5; ++i) {
			auto *replacement = new QMenu;
			replacement->addAction(QStringLiteral("Replacement"));
			tray.setContextMenu(replacement);
			delete replacement; // Detach the retained NSMenu before its owner disappears.
		}
		tray.setContextMenu(&menu);
	}
}

}

int main(int argc, char **argv)
{
	@autoreleasepool {
		[PastesTrayTestApplication sharedApplication];
		QApplication app(argc, argv);
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
		const bool legacy = app.arguments().contains(QStringLiteral("--legacy-qt-tray"));
		return runTest("native tray tracking with non-mouse events and action delivery",
			[&] { repeatedMenuTracking(legacy); });
	}
}
