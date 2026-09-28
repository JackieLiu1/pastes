#ifndef PLATFORM_APPLICATIONINTEGRATION_H
#define PLATFORM_APPLICATIONINTEGRATION_H

namespace Platform {

struct ApplicationBehavior
{
	bool showOnLaunch = false;
	bool showOnActivation = false;
	bool showOnSecondInstance = false;
};

const ApplicationBehavior &applicationBehavior(void);
void configureApplication(void);

}
#endif
