#include "platform/startupintegration.h"

#include <QSettings>
#include <QDir>
#include <QObject>

bool StartupIntegration::supported(void) const
{
	return true;
}

QString StartupIntegration::statusMessage(void) const
{
	return QString();
}

bool StartupIntegration::enabled(void) const
{
	QSettings user(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
	if (!user.value(m_entry_name).toString().trimmed().isEmpty()) return true;
	for (QSettings::Format view : {QSettings::Registry64Format, QSettings::Registry32Format}) {
		QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), view);
		if (!machine.value(m_entry_name).toString().trimmed().isEmpty()) return true;
	}
	return false;
}

bool StartupIntegration::setEnabledNative(bool enabled, QString *error)
{
	/* Old installers created an all-user entry. Never report a successful
	 * disable while that entry would still launch the application. */
	if (!enabled) {
		for (QSettings::Format view : {QSettings::Registry64Format, QSettings::Registry32Format}) {
			QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), view);
			if (machine.value(m_entry_name).toString().trimmed().isEmpty()) continue;
			machine.remove(m_entry_name); machine.sync();
			if (machine.status() != QSettings::NoError) {
				if (error) *error = QObject::tr("An older installation enabled startup for all users. Reinstall the current version to manage startup for your account.");
				return false;
			}
		}
	}
	QSettings user(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
	if (enabled) {
		const QString command = QStringLiteral("\"")+QDir::toNativeSeparators(m_executable)+QStringLiteral("\"");
		if (command.size() > 260 || command.contains('\n') || command.contains('\r') || m_executable.contains('"')) {
			if (error) *error = QObject::tr("This application path cannot be used for startup.");
			return false;
		}
		user.setValue(m_entry_name, command);
	} else {
		user.remove(m_entry_name);
	}
	user.sync();
	if (user.status() != QSettings::NoError) {
		if (error) *error = QObject::tr("Could not update startup. Check your account permissions and try again.");
		return false;
	}
	return true;
}
