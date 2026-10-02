#include "tests/testsupport.h"
#include "platform/applicationintegration.h"
#include "platform/trayicon.h"
#include "application/clipboardcontroller.h"
#include "application/historyservice.h"
#include "ui/mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
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

class IdleFeed final : public ClipboardFeed
{
public:
	int settleInterval(void) const override { return 0; }
	void capture(quint64) override {}
	bool synchronize(void) override { return false; }
	bool allowsCapture(void) const override { return false; }
	QImage snapshotIcon(void) override { return {}; }
};

void nativeHistoryToggle(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	IdleFeed feed;
	ClipboardController clipboard(history, feed, *QApplication::clipboard(), false);
	MainWindow window(history, clipboard);
	auto *action = window.findChild<QAction *>("ShowHistoryAction");
	require(action, "Native tray history action missing");
	QMenu *menu = nullptr;
	for (QMenu *candidate : window.findChildren<QMenu *>()) {
		if (candidate->actions().contains(action)) {
			menu = candidate;
			break;
		}
	}
	require(menu, "Native tray menu missing");
	NSMenu *native = menu->toNSMenu();
	const QString showLabel = action->text();
	for (int i = 0; i < 2; ++i) {
		require([native.itemArray.firstObject.title isEqualToString:showLabel.toNSString()],
			"Native menu did not offer show history");
		[native performActionForItemAtIndex:0];
		waitUntil([&] { return window.isVisible(); });
		require(action->text() != showLabel &&
			[native.itemArray.firstObject.title isEqualToString:action->text().toNSString()],
			"Native menu did not update to hide history");
		[native performActionForItemAtIndex:0];
		waitUntil([&] { return !window.isVisible(); });
		require(action->text() == showLabel,
			"Native menu did not hide history or reset its title");
	}
}

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
		QTemporaryDir preferences;
		QCoreApplication::setOrganizationName("PastesTrayTests");
		QCoreApplication::setApplicationName("NativeMenu");
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
		const bool legacy = app.arguments().contains(QStringLiteral("--legacy-qt-tray"));
		int failures = runTest("native tray tracking with non-mouse events and action delivery",
			[&] { repeatedMenuTracking(legacy); });
		if (!legacy)
			failures += runTest("native tray menu shows and hides history with matching titles",
				nativeHistoryToggle);
		return failures ? 1 : 0;
	}
}
