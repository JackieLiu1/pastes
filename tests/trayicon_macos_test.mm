#include "tests/testsupport.h"
#include "platform/applicationintegration.h"
#include "platform/trayicon.h"
#include "application/clipboardcontroller.h"
#include "application/historyservice.h"
#include "ui/mainwindow.h"
#include "ui/historyview.h"
#include "ui/pasteitem.h"
#include <QContextMenuEvent>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

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


void selectCardMenu(PasteItem *card, const QString &label)
{
	__block bool selected = false;
	id observer = [NSNotificationCenter.defaultCenter
		addObserverForName:NSMenuDidBeginTrackingNotification object:nil queue:nil
		usingBlock:^(NSNotification *notification) {
			NSMenu *menu = notification.object;
			if (![menu isKindOfClass:NSMenu.class]) return;
			const NSInteger index = [menu indexOfItemWithTitle:label.toNSString()];
			if (index < 0) return;
			NSTimer *timer = [NSTimer timerWithTimeInterval:0.05 repeats:NO block:^(NSTimer *) {
				selected = true; [menu performActionForItemAtIndex:index]; [menu cancelTracking];
			}];
			[NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
		}];
	QContextMenuEvent event(QContextMenuEvent::Mouse, QPoint(20, 20), card->mapToGlobal(QPoint(20, 20)));
	QCoreApplication::sendEvent(card, &event);
	[NSNotificationCenter.defaultCenter removeObserver:observer];
	require(selected, "Native card menu action was not delivered");
}

void nativeFavoriteMenu(void)
{
	MemoryRepository repository; HistoryService history(repository);
	QWidget window; window.resize(1200, 430);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window); layout.addWidget(&view);
	history.load(); repository.finishLoad({textEntry("native favorite menu fixture")});
	window.show(); window.activateWindow();
	auto *list = view.findChild<QListWidget *>();
	auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->item(0)));
	for (bool expected : {true, false}) {
		selectCardMenu(card, expected ? QObject::tr("Add to Favorites") : QObject::tr("Remove from Favorites"));
		require(card->entry()->favorite == expected && !card->isHidden() && list->currentItem() == card->widgetItem() &&
			card->findChild<QPushButton *>("FavoriteButton")->isHidden(),
			"Native favorite menu lost its state, card or selection, or displayed a star in history");
	}
	history.setFavorite(card->entry()->id, true);
	auto other = textEntry("second native favorite"); history.record(other); history.setFavorite(other->id, true);
	view.findChild<QPushButton *>("FavoritesTab")->click();
	require(card->findChild<QPushButton *>("FavoriteButton")->isVisible(),
		"Favorites did not show its star after native menu changes");
	HistoryEntry renamed;
	QObject::connect(&view, &HistoryView::renameFavoriteRequested, &view, [&](HistoryEntry entry) { renamed = entry; });
	selectCardMenu(card, QObject::tr("Name Favorite…"));
	require(renamed == card->entry(), "Native rename menu lost the selected entry");
	selectCardMenu(card, QObject::tr("Move Right"));
	require(list->row(card->widgetItem()) == 1 && list->currentItem() == card->widgetItem(),
		"Native reorder menu lost fixed order or selected card");
	selectCardMenu(card, QObject::tr("Move Left"));
	require(list->row(card->widgetItem()) == 0, "Native reorder menu could not move back");
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
		const bool accessoryBeforeConfiguration =
			NSApp.activationPolicy == NSApplicationActivationPolicyAccessory;
		Platform::configureApplication();
		QTemporaryDir preferences;
		QCoreApplication::setOrganizationName("PastesTrayTests");
		QCoreApplication::setApplicationName("NativeMenu");
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
		const bool legacy = app.arguments().contains(QStringLiteral("--legacy-qt-tray"));
		int failures = runTest("menu-bar launch has no Dock presence before configuration", [&] {
			require(accessoryBeforeConfiguration,
				"Application started as a regular Dock app before runtime configuration");
			if (!legacy) {
				require(app.arguments().size() > 1, "Production bundle path missing");
				NSBundle *bundle = [NSBundle bundleWithPath:app.arguments().at(1).toNSString()];
				require([[bundle objectForInfoDictionaryKey:@"LSUIElement"] boolValue],
					"Production bundle did not declare a menu-bar agent application");
			}
		});
		failures += runTest("native tray tracking with non-mouse events and action delivery",
			[&] { repeatedMenuTracking(legacy); });
		if (!legacy)
			failures += runTest("native favorite menu add and remove", nativeFavoriteMenu);
		if (!legacy)
			failures += runTest("native tray menu shows and hides history with matching titles",
				nativeHistoryToggle);
		failures += runTest("native windows and menus preserve the menu-bar activation policy", [] {
			require(NSApp.activationPolicy == NSApplicationActivationPolicyAccessory,
				"Showing a window or menu restored the Dock activation policy");
		});
		return failures ? 1 : 0;
	}
}
