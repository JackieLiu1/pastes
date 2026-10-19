#ifndef FILEPREVIEW_H
#define FILEPREVIEW_H

#include <QImage>
#include <QString>
#include <QUrl>

namespace FilePreview {

/* Display hints only: never change the clipboard's original MIME data. */
QUrl localUrl(const QString &text);
bool isSvg(const QUrl &url);
QImage loadImage(const QUrl &url, int maxPixels);

}
#endif
