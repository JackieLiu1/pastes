#include "platform/startupintegration.h"

#include <QObject>

bool StartupIntegration::supported(void) const { return false; }
bool StartupIntegration::enabled(void) const { return false; }

bool StartupIntegration::setEnabledNative(bool enabled, QString *error)
{
	Q_UNUSED(enabled);
	if (error) *error = QObject::tr("Startup is not available on this platform yet.");
	return false;
}
