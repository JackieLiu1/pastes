#ifndef PASTES_LINUX_CLIPBOARDICON_H
#define PASTES_LINUX_CLIPBOARDICON_H

#include <QImage>
#include <QStringList>

namespace LinuxClipboardIcon {

/* Values captured before the clipboard debounce; no native handles escape. */
struct Source
{
	quint32 owner = 0;
	quint32 processId = 0;
	QImage icon;
	QStringList classes;
	QStringList desktopIds;
	QString executable;
};

QImage decodeIcon(const quint32 *data, qsizetype count);
Source captureSource(void);
/* GIO lookup runs on the worker. Theme rendering belongs to the GUI thread. */
QString desktopIcon(const Source &source);

}

#endif
