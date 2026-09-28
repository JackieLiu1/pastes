#include "platform/paths.h"

#include <cstdlib>

#ifndef QM_FILES_INSTALL_PATH
#define QM_FILES_INSTALL_PATH "."
#endif

QString Platform::databasePath(void)
{
	return QString(getenv("HOME"))+"/.cache/PastesDatabase.db";
}

QString Platform::translationDirectory(void)
{
	return QStringLiteral(QM_FILES_INSTALL_PATH);
}
