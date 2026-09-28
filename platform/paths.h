#ifndef PLATFORM_PATHS_H
#define PLATFORM_PATHS_H

#include <QString>

namespace Platform {

QString databasePath(void);
QString translationDirectory(void);
/* Called by Database::Worker before opening its connection. No SQL or UI. */
bool prepareDatabaseDirectory(const QString &databasePath);

}
#endif
