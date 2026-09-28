#include "platform/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString Platform::databasePath(void)
{
	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		+ "/PastesDatabase.db";
}

QString Platform::translationDirectory(void)
{
	return QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources");
}

bool Platform::prepareDatabaseDirectory(const QString &path)
{
	return QDir().mkpath(QFileInfo(path).absolutePath());
}
