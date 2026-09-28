#include "platform/paths.h"

#include <QCoreApplication>

QString Platform::databasePath(void)
{
	return QCoreApplication::applicationDirPath()+"/PastesDatabase.db";
}

QString Platform::translationDirectory(void)
{
	return QCoreApplication::applicationDirPath();
}

bool Platform::prepareDatabaseDirectory(const QString &path)
{
	Q_UNUSED(path);
	return true;
}
