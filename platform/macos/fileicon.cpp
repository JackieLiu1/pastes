#include "platform/fileicon.h"

QIcon Platform::fileIcon(const QString &path)
{
	Q_UNUSED(path);
	/* Keep the existing fallback; image file previews are handled by Qt. */
	return QIcon();
}
