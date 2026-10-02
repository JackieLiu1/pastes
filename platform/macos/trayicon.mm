#include "platform/trayicon.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QPixmap>
#import <AppKit/AppKit.h>

class TrayIcon::NativeState
{
public:
	~NativeState()
	{
		if (!item) return;
		[item.menu cancelTracking];
		item.menu = nil;
		[NSStatusBar.systemStatusBar removeStatusItem:item];
		[item release];
	}
	NSStatusItem *item = nil;
	QIcon icon;
	QString toolTip;
	QPointer<QMenu> menu;
	QMetaObject::Connection menuDestroyed;
};

TrayIcon::TrayIcon(QObject *parent) : QObject(parent),
	m_native(std::make_unique<NativeState>()) {}

TrayIcon::~TrayIcon() = default;

void TrayIcon::setIcon(const QIcon &icon)
{
	m_native->icon = icon;
	if (!m_native->item) return;
	const int height = qMax(1, qRound(NSStatusBar.systemStatusBar.thickness)-4);
	const QPixmap pixmap = icon.pixmap(QSize(height, height), qGuiApp->devicePixelRatio());
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	if (!pixmap.toImage().save(&buffer, "PNG")) {
		m_native->item.button.image = nil;
		return;
	}
	@autoreleasepool {
		NSData *data = [NSData dataWithBytes:bytes.constData() length:bytes.size()];
		NSImage *image = [[[NSImage alloc] initWithData:data] autorelease];
		image.size = NSMakeSize(pixmap.width()/pixmap.devicePixelRatio(),
			pixmap.height()/pixmap.devicePixelRatio());
		[image setTemplate:icon.isMask()];
		m_native->item.button.image = image;
		m_native->item.button.imageScaling = NSImageScaleProportionallyDown;
	}
}

void TrayIcon::setToolTip(const QString &toolTip)
{
	m_native->toolTip = toolTip;
	if (m_native->item)
		m_native->item.button.toolTip = toolTip.toNSString();
}

void TrayIcon::setContextMenu(QMenu *menu)
{
	disconnect(m_native->menuDestroyed);
	m_native->menu = menu;
	if (menu) {
		m_native->menuDestroyed = connect(menu, &QObject::destroyed, this, [this](void) {
			setContextMenu(nullptr);
		});
	}
	if (m_native->item)
		m_native->item.menu = menu ? menu->toNSMenu() : nil;
}

void TrayIcon::show(void)
{
	if (!isAvailable() || m_native->item) return;
	m_native->item = [[NSStatusBar.systemStatusBar
		statusItemWithLength:NSSquareStatusItemLength] retain];
	m_native->item.button.accessibilityLabel = @"Pastes";
	setIcon(m_native->icon);
	setToolTip(m_native->toolTip);
	/* Let AppKit track the attached menu. Do not inspect currentEvent or
	 * infer mouse clicks from menu-tracking notifications: macOS 27 can
	 * start tracking from a non-mouse event. */
	setContextMenu(m_native->menu.data());
}

bool TrayIcon::isAvailable(void)
{
	return QGuiApplication::platformName() == QStringLiteral("cocoa");
}
