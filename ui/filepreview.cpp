#include "ui/filepreview.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>

QUrl FilePreview::localUrl(const QString &text)
{
	QString path = text.trimmed();
	if (path.isEmpty() || path.contains('\n') || path.contains('\r') || path.contains(QChar::Null))
		return QUrl();
	if (path.size() >= 2 && ((path.front() == '"' && path.back() == '"') ||
				(path.front() == '\'' && path.back() == '\'')))
		path = path.mid(1, path.size()-2);
	const QUrl url(path, QUrl::StrictMode);
	if (url.isLocalFile()) {
		if (!url.host().isEmpty() && url.host().compare("localhost", Qt::CaseInsensitive) != 0)
			return QUrl();
		path = url.host().isEmpty() ? url.toLocalFile() : url.path(QUrl::FullyDecoded);
	} else if (path.startsWith("~/")) {
		path = QDir::homePath() + path.mid(1);
	}
	const QFileInfo file(path);
	/* Relative paths depend on another app's working directory. Do not
	 * guess, fetch remote URLs, or turn prose containing a path into a file. */
	if (!file.isAbsolute() || !file.exists())
		return QUrl();
	return QUrl::fromLocalFile(file.absoluteFilePath());
}

QImage FilePreview::loadImage(const QUrl &url, int maxPixels)
{
	if (!url.isLocalFile() || maxPixels <= 0)
		return QImage();
	QImageReader reader(url.toLocalFile());
	reader.setAutoTransform(true);
	if (!reader.canRead())
		return QImage();
	QSize size = reader.size();
	if (size.isValid() && (size.width() > maxPixels || size.height() > maxPixels)) {
		size.scale(maxPixels, maxPixels, Qt::KeepAspectRatio);
		reader.setScaledSize(size);
	}
	return reader.read();
}
