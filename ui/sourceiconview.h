#ifndef SOURCEICONVIEW_H
#define SOURCEICONVIEW_H

#include <QImage>
#include <QPixmap>

namespace SourceIconView {

/* Older Linux histories stored this placeholder as if it were a real app. */
bool isLegacyPlaceholder(const QImage &image);
/* Call only when the source or device pixel ratio changes. */
QPixmap pixmap(const QImage &image, int logicalSize, qreal devicePixelRatio);

}

#endif
