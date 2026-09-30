#ifndef SOURCEICONVIEW_H
#define SOURCEICONVIEW_H

#include <QImage>
#include <QPixmap>

namespace SourceIconView {

/* Call only when the source or device pixel ratio changes. */
QPixmap pixmap(const QImage &image, int logicalSize, qreal devicePixelRatio);

}

#endif
