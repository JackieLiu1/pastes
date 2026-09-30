#ifndef PASTES_MACOS_CLIPBOARDMETADATA_H
#define PASTES_MACOS_CLIPBOARDMETADATA_H

#include <QJsonObject>
#include <QString>

@class NSPasteboard;

namespace Platform {
/* Extract declarations without requesting any promised content. */
QJsonObject clipboardMetadata(NSPasteboard *pasteboard, bool allowed, const QString &foreground);
}

#endif
