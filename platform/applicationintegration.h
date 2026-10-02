#ifndef PLATFORM_APPLICATIONINTEGRATION_H
#define PLATFORM_APPLICATIONINTEGRATION_H

class QIcon;

namespace Platform {

struct ApplicationBehavior
{
	bool showOnLaunch = false;
	bool showOnActivation = false;
	bool showOnSecondInstance = false;
};

const ApplicationBehavior &applicationBehavior(void);
void configureApplication(void);
QIcon trayIcon(void);

}
#endif
