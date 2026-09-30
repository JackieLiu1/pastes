#include "ui/filepreview.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>

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

bool FilePreview::isSvg(const QUrl &url)
{
	return url.isLocalFile() &&
		QMimeDatabase().mimeTypeForFile(url.fileName(), QMimeDatabase::MatchExtension).inherits("image/svg+xml");
}

QImage FilePreview::loadImage(const QUrl &url, int maxPixels)
{
	if (!url.isLocalFile() || maxPixels <= 0)
		return QImage();
	/* Missing files, folders and special files retain their file or path
	 * representation rather than creating a picture preview. */
	const QFileInfo file(url.toLocalFile());
	if (!file.isFile())
		return QImage();
	/* Image plugins may also decode documents such as PDF. Only actual
	 * image content belongs in the picture preview. */
	const QMimeType type = QMimeDatabase().mimeTypeForFile(file, QMimeDatabase::MatchContent);
	if (!type.name().startsWith("image/") &&
		!(isSvg(url) && (type.inherits("application/xml") || type.inherits("application/gzip"))))
		return QImage();
	QImageReader reader(url.toLocalFile());
	reader.setAutoTransform(true);
	if (!reader.canRead())
		return QImage();
	QSize size = reader.size();
	const QByteArray format = reader.format();
	/* Vector artwork is rendered at the requested resolution, including
	 * small intrinsic canvases. Scaling its original raster would blur it. */
	const bool vector = format == "svg" || format == "svgz";
	if (size.isValid() && (vector || size.width() > maxPixels || size.height() > maxPixels)) {
		size.scale(maxPixels, maxPixels, Qt::KeepAspectRatio);
		reader.setScaledSize(size);
	}
	return reader.read();
}
