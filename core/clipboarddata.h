#ifndef CLIPBOARDDATA_H
#define CLIPBOARDDATA_H

#include <QByteArray>
#include <QImage>
#include <QMimeData>
#include <memory>

namespace ClipboardData {

/* Encoded originals are immutable. Only explicit image requests decode them;
 * cards use a bounded preview and clones share the encoded bytes. */
constexpr int previewPixels = 1024;
[[nodiscard]] std::unique_ptr<QMimeData> duplicate(const QMimeData *source, bool includeImage = true);
[[nodiscard]] std::unique_ptr<QMimeData> withStoredImage(const QMimeData *source,
	const QByteArray &encoded, int format, qreal ratio = 1);
QByteArray storedImage(const QMimeData *source);
QSize imageSize(const QMimeData *source);
QImage previewImage(const QMimeData *source, QSize *originalSize);
bool canCompressImage(const QImage &image);

}

#endif
