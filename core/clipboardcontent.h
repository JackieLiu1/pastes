#ifndef PASTES_CLIPBOARDCONTENT_H
#define PASTES_CLIPBOARDCONTENT_H

#include <QByteArray>
class QMimeData;
class QImage;

namespace ClipboardContent {

/* This is the persisted deduplication identity; preserve format precedence,
 * text encoding and every image row byte when changing its implementation. */
QByteArray fingerprint(const QMimeData &mime);
QByteArray imageFingerprint(const QImage &image);

}

#endif
