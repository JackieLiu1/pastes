#include "platform/applicationintegration.h"

#include <QApplication>
#include <QIcon>
#import <AppKit/AppKit.h>

const Platform::ApplicationBehavior &Platform::applicationBehavior(void)
{
	static const ApplicationBehavior behavior = [] {
		ApplicationBehavior value;
		value.showOnLaunch = true;
		value.showOnActivation = true;
		value.showOnSecondInstance = true;
		return value;
	}();
	return behavior;
}

void Platform::configureApplication(void)
{
	qApp->setWindowIcon(QIcon(":/resources/pastes.svg"));
	if (QGuiApplication::platformName() == QStringLiteral("cocoa"))
		[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

QIcon Platform::trayIcon(void)
{
	QIcon icon(QStringLiteral(":/resources/pastes-menubar.svg"));
	// Cocoa renders mask icons as template images for the menu bar appearance.
	icon.setIsMask(true);
	return icon;
}
