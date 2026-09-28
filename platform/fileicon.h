#ifndef PLATFORM_FILEICON_H
#define PLATFORM_FILEICON_H

#include <QIcon>
#include <QString>

namespace Platform {
/* GUI thread only: native icon conversion may create QPixmap values. */
QIcon fileIcon(const QString &path);
}

#endif
