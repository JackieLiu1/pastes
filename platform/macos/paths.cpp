#include "platform/paths.h"

#include <QCoreApplication>
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
