#include "platform/menuintegration.h"

#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QWidget>
#include <qpa/qplatformmenu.h>
#import <AppKit/AppKit.h>

static NSView *nativeView(QWidget *widget)
{
	if (QGuiApplication::platformName() != QStringLiteral("cocoa") ||
		!widget->testAttribute(Qt::WA_WState_Created))
		return nil;
	return reinterpret_cast<NSView *>(widget->winId());
}

static bool popupNativeMenu(QMenu *menu, QWidget *owner,
			    const QPoint &point, bool alignRight)
{
	NSView *view = nativeView(owner->window());
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
	const QPoint location(point.x()-(alignRight ? nativeMenu.size.width : 0), point.y());
	/* Cocoa menu tracking consumes mouse-up. Let Qt's native popup backend
	 * restore its view's button state before returning to card input. */
	menu->platformMenu()->showPopup(owner->window()->windowHandle(),
		QRect(location, QSize(0, 0)), nullptr);
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
	if (!popupNativeMenu(menu, anchor, point, true))
		menu->exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

QAction *Platform::execMenuAt(QMenu *menu, QWidget *owner, const QPoint &position)
{
	QPointer<QAction> chosen;
	const auto connection = QObject::connect(menu, &QMenu::triggered, menu,
		[&chosen](QAction *action) { chosen = action; });
	const bool native = popupNativeMenu(menu, owner,
		owner->window()->mapFromGlobal(position), false);
	QObject::disconnect(connection);
	return native ? chosen.data() : menu->exec(position);
}
