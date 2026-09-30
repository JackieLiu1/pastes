#ifndef CLIPBOARDDATA_H
#define CLIPBOARDDATA_H

#include <QByteArray>
#include <QImage>
#include <QMimeData>

namespace ClipboardData {

/* Encoded originals are immutable. Only explicit image requests decode them;
 * cards use a bounded preview and clones share the encoded bytes. */
constexpr int previewPixels = 1024;
QMimeData *duplicate(const QMimeData *source, bool includeImage = true);
QMimeData *withStoredImage(const QMimeData *source, const QByteArray &encoded, int format, qreal ratio = 1);
QByteArray storedImage(const QMimeData *source);
QImage previewImage(const QMimeData *source, QSize *originalSize);
bool canCompressImage(const QImage &image);

}

#endif
