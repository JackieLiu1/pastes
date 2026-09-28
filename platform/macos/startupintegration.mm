#include "platform/startupintegration.h"

#include <QFileInfo>
#include <QObject>
#import <Foundation/Foundation.h>
#import <ServiceManagement/ServiceManagement.h>

static bool currentApplication(const QString &executable)
{
	NSBundle *bundle = NSBundle.mainBundle;
	return [bundle.bundlePath.pathExtension isEqualToString:@"app"] &&
		bundle.bundleIdentifier.length > 0 &&
		QFileInfo(executable).canonicalFilePath() ==
		QFileInfo(QString::fromNSString(bundle.executablePath)).canonicalFilePath();
}

bool StartupIntegration::supported(void) const
{
	if (@available(macOS 13.0, *))
		return currentApplication(m_executable);
	return false;
}

bool StartupIntegration::enabled(void) const
{
	if (@available(macOS 13.0, *))
		return supported() && SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
	return false;
}

QString StartupIntegration::statusMessage(void) const
{
	if (@available(macOS 13.0, *)) {
		if (!currentApplication(m_executable))
			return QObject::tr("Open the Pastes application to manage launch at sign-in.");
		if (SMAppService.mainAppService.status == SMAppServiceStatusRequiresApproval)
			return QObject::tr("Allow Pastes in System Settings > General > Login Items to launch at sign-in.");
		return QString();
	}
	return QObject::tr("Launch at sign-in requires macOS 13 or later.");
}

bool StartupIntegration::setEnabledNative(bool enabled, QString *error)
{
	if (!supported()) {
		if (error) *error = statusMessage();
		return false;
	}
	if (@available(macOS 13.0, *)) {
		SMAppService *service = SMAppService.mainAppService;
		const SMAppServiceStatus status = service.status;
		if ((enabled && status == SMAppServiceStatusEnabled) ||
			(!enabled && (status == SMAppServiceStatusNotRegistered || status == SMAppServiceStatusNotFound)))
			return true;
		/* A revoked system permission must be restored by the user. */
		if (enabled && status == SMAppServiceStatusRequiresApproval) {
			if (error) *error = statusMessage();
			return false;
		}
		NSError *nativeError = nil;
		const bool changed = enabled ? [service registerAndReturnError:&nativeError] :
			[service unregisterAndReturnError:&nativeError];
		if (changed && this->enabled() == enabled)
			return true;
		QString message = statusMessage();
		if (message.isEmpty())
			message = nativeError ? QObject::tr("Could not update launch at sign-in: %1")
				.arg(QString::fromNSString(nativeError.localizedDescription)) :
				QObject::tr("The system did not apply the launch at sign-in change. Try again.");
		if (error) *error = message;
	}
	return false;
}
