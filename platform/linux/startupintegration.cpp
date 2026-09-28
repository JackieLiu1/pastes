#include "platform/startupintegration.h"

#include <QSettings>
#include <QDir>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>

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
	const QString relative = QStringLiteral("autostart/")+m_entry_name.toLower()+QStringLiteral(".desktop");
	const QString path = QStandardPaths::locate(QStandardPaths::GenericConfigLocation, relative);
	if (path.isEmpty()) return false;
	QSettings entry(path, QSettings::IniFormat);
	entry.beginGroup(QStringLiteral("Desktop Entry"));
	return !entry.value(QStringLiteral("Hidden"), false).toBool() &&
		entry.value(QStringLiteral("X-GNOME-Autostart-enabled"), true).toBool();
}

bool StartupIntegration::setEnabledNative(bool enabled, QString *error)
{
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
}
