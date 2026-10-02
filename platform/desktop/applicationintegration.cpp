#include "platform/applicationintegration.h"

#include <QIcon>

const Platform::ApplicationBehavior &Platform::applicationBehavior(void)
{
	static const ApplicationBehavior behavior;
	return behavior;
}

void Platform::configureApplication(void) {}

QIcon Platform::trayIcon(void)
{
	return QIcon(QStringLiteral(":/resources/pastes.svg"));
}
