#include "platform/fileicon.h"

#include <QFileIconProvider>
#include <QFileInfo>

QIcon Platform::fileIcon(const QString &path)
{
	/* Qt's Cocoa provider preserves native file types and Retina sizes. */
	QFileIconProvider provider;
	return provider.icon(QFileInfo(path));
}
