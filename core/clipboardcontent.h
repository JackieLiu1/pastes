#ifndef PASTES_CLIPBOARDCONTENT_H
#define PASTES_CLIPBOARDCONTENT_H

#include <QByteArray>
class QMimeData;

namespace ClipboardContent {

/* This is the persisted deduplication identity; preserve format precedence,
 * text encoding and every image row byte when changing its implementation. */
QByteArray fingerprint(const QMimeData &mime);

}

#endif
