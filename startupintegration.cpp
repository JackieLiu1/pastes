#include "startupintegration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

StartupIntegration::StartupIntegration(const QString &executable, const QString &entryName) :
	m_executable(executable.isEmpty() ? QCoreApplication::applicationFilePath() : executable),
	m_entry_name(entryName)
{
}

bool StartupIntegration::supported(void) const
{
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
	return true;
#else
	return false;
#endif
}

bool StartupIntegration::enabled(void) const
{
#ifdef Q_OS_WIN
	QSettings user(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
	if (!user.value(m_entry_name).toString().trimmed().isEmpty()) return true;
	for (QSettings::Format view : {QSettings::Registry64Format, QSettings::Registry32Format}) {
		QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), view);
		if (!machine.value(m_entry_name).toString().trimmed().isEmpty()) return true;
	}
	return false;
#elif defined(Q_OS_LINUX)
	const QString relative = QStringLiteral("autostart/")+m_entry_name.toLower()+QStringLiteral(".desktop");
	const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, relative);
	if (path.isEmpty()) return false;
	QSettings entry(path, QSettings::IniFormat);
	entry.beginGroup(QStringLiteral("Desktop Entry"));
	return !entry.value(QStringLiteral("Hidden"), false).toBool() &&
		entry.value(QStringLiteral("X-GNOME-Autostart-enabled"), true).toBool();
#else
	return false;
#endif
}

bool StartupIntegration::setEnabled(bool enabled, QString *error)
{
	if (error) error->clear();
	if (m_executable.contains('\n') || m_executable.contains('\r')) {
		if (error) *error = QObject::tr("This application path cannot be used for startup.");
		return false;
	}
#ifdef Q_OS_WIN
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
#elif defined(Q_OS_LINUX)
	const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)+QStringLiteral("/autostart");
	if (!QDir().mkpath(directory)) {
		if (error) *error = QObject::tr("Could not create the startup folder.");
		return false;
	}
	QString command = m_executable;
	command.replace(QStringLiteral("\\"), QStringLiteral("\\\\\\\\"));
	command.replace(QStringLiteral("\""), QStringLiteral("\\\\\""));
	command.replace(QStringLiteral("$"), QStringLiteral("\\\\$"));
	command.replace(QStringLiteral("`"), QStringLiteral("\\\\`"));
	command.replace(QStringLiteral("%"), QStringLiteral("%%"));
	QSaveFile file(directory+'/'+m_entry_name.toLower()+QStringLiteral(".desktop"));
	const QByteArray contents = (QStringLiteral("[Desktop Entry]\nType=Application\nName=Pastes\nExec=\"")+command+
		QStringLiteral("\"\nIcon=pastes\nTerminal=false\nHidden=")+(enabled ? QStringLiteral("false\n") : QStringLiteral("true\n"))).toUtf8();
	if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
		if (error) *error = QObject::tr("Could not save startup: %1").arg(file.errorString());
		return false;
	}
	return true;
#else
	Q_UNUSED(enabled);
	if (error) *error = QObject::tr("Startup is not available on this platform yet.");
	return false;
#endif
}
