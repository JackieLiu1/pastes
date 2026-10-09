#include "platform/applicationintegration.h"

#include <QApplication>
#include <QIcon>

const Platform::ApplicationBehavior &Platform::applicationBehavior(void)
{
	static const ApplicationBehavior behavior;
	return behavior;
}

void Platform::configureApplication(void)
{
	qApp->setWindowIcon(trayIcon());
	QGuiApplication::setDesktopFileName(QStringLiteral("pastes"));
}

QIcon Platform::trayIcon(void)
{
	return QIcon(QStringLiteral(":/resources/pastes.svg"));
}
