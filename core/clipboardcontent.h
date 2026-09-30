#ifndef PASTES_CLIPBOARDCONTENT_H
#define PASTES_CLIPBOARDCONTENT_H

#include <QByteArray>
class QMimeData;
class QImage;

namespace ClipboardContent {

/* Supplied pixels remain usable independently of an accompanying local file.
 * File references alone never cause a file read or become image content. */
bool prefersImage(const QMimeData &mime);

/* This is the persisted deduplication identity; preserve format precedence,
 * text encoding and every image row byte when changing its implementation. */
QByteArray fingerprint(const QMimeData &mime);
QByteArray imageFingerprint(const QImage &image);

/* Pixel comparison is independent of the persisted MIME fingerprint and
 * ignores packing, row padding and an accompanying temporary file path. */
QByteArray imageContentKey(const QImage &image);

}

#endif
