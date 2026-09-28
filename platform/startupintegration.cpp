#include "platform/startupintegration.h"

#include <QCoreApplication>

StartupIntegration::StartupIntegration(const QString &executable, const QString &entryName) :
	m_executable(executable.isEmpty() ? QCoreApplication::applicationFilePath() : executable),
	m_entry_name(entryName)
{
}

bool StartupIntegration::setEnabled(bool enabled, QString *error)
{
	if (error) error->clear();
	if (m_executable.contains('\n') || m_executable.contains('\r')) {
		if (error) *error = QObject::tr("This application path cannot be used for startup.");
		return false;
	}
	return setEnabledNative(enabled, error);
}
