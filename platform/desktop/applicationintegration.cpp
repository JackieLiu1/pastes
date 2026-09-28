#include "platform/applicationintegration.h"

const Platform::ApplicationBehavior &Platform::applicationBehavior(void)
{
	static const ApplicationBehavior behavior;
	return behavior;
}

void Platform::configureApplication(void) {}
