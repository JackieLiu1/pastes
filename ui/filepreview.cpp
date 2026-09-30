#include "ui/filepreview.h"

#include <QDir>
#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <cmath>

namespace {
QColor hslColor(const QString &value)
{
	static const QRegularExpression function("^hsla?\\((.*)\\)$", QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression separator("[\\s,/]+");
	static const QRegularExpression number("^([+-]?(?:\\d*\\.\\d+|\\d+\\.?\\d*)(?:[eE][+-]?\\d+)?)(deg|grad|rad|turn)?$",
		QRegularExpression::CaseInsensitiveOption);
	const auto match = function.match(value.trimmed());
	if (!match.hasMatch()) return {};
	const auto parts = match.captured(1).trimmed().split(separator, Qt::SkipEmptyParts);
	if (parts.size() != 3 && parts.size() != 4) return {};
	const auto hue = number.match(parts[0]);
	if (!hue.hasMatch() || !parts[1].endsWith('%') || !parts[2].endsWith('%')) return {};
	bool ok = false;
	double h = hue.captured(1).toDouble(&ok);
	if (!ok || !std::isfinite(h)) return {};
	const QString unit = hue.captured(2).toLower();
	if (unit == "turn") h *= 360;
	else if (unit == "grad") h *= 0.9;
	else if (unit == "rad") h *= 180/std::acos(-1.0);
	if (!std::isfinite(h)) return {};
	h = std::fmod(h, 360);
	if (h < 0) h += 360;
	auto component = [&](QString text, double &result) {
		const bool percent = text.endsWith('%');
		if (percent) text.chop(1);
		if (!number.match(text).hasMatch()) return false;
		result = text.toDouble(&ok);
		if (!ok || !std::isfinite(result)) return false;
		result = qBound(0.0, result/(percent ? 100 : 1), 1.0);
		return true;
	};
	double s, l, a = 1;
	if (!component(parts[1], s) || !component(parts[2], l) ||
		(parts.size() == 4 && !component(parts[3], a))) return {};
	return QColor::fromHslF(h/360, s, l, a);
}

QByteArray svgColorData(const QByteArray &source, const QUrl &url)
{
	/* Qt SVG accepts RGB paint but not CSS HSL functions. Rewrite only
	 * fill/stroke paint in a display copy; never edit text or clipboard data. */
	QXmlStreamReader reader(source);
	QByteArray output;
	QXmlStreamWriter writer(&output);
	const QString prefix = "pastes-svg-color-"+QUuid::createUuid().toString(QUuid::Id128)+"-";
	QList<QColor> paints;
	bool changed = false;
	auto paint = [&](const QString &value) {
		const QColor color = hslColor(value);
		if (!color.isValid()) return value;
		changed = true;
		if (color.alphaF() == 1) return color.name();
		int index = paints.indexOf(color);
		if (index < 0) { index = paints.size(); paints.append(color); }
		/* A constant gradient preserves color alpha independently of inherited
		 * and explicit fill/stroke opacity. An opacity attribute would replace it. */
		return "url(#"+prefix+QString::number(index)+")";
	};
	static const QRegularExpression declaration(
		"(^|[;{])(\\s*(?:fill|stroke)\\s*:\\s*)(hsla?\\([^()]*\\))(?=\\s*(?:!important\\s*)?(?:[;}]|$))",
		QRegularExpression::CaseInsensitiveOption);
	auto css = [&](const QString &text) {
		QString result;
		qsizetype position = 0;
		auto matches = declaration.globalMatch(text);
		while (matches.hasNext()) {
			const auto match = matches.next();
			result += text.mid(position, match.capturedStart(3)-position)+paint(match.captured(3));
			position = match.capturedEnd(3);
		}
		return result+text.mid(position);
	};
	int depth = 0;
	QString svgPrefix;
	while (!reader.atEnd()) {
		reader.readNext();
		if (reader.isStartElement()) {
			++depth;
			if (depth == 1) svgPrefix = reader.prefix().isEmpty() ? QString() : reader.prefix().toString()+":";
			writer.writeStartElement(reader.qualifiedName().toString());
			for (const auto &ns : reader.namespaceDeclarations())
				writer.writeNamespace(ns.namespaceUri().toString(), ns.prefix().toString());
			for (const auto &attribute : reader.attributes()) {
				QString value = attribute.value().toString();
				if (attribute.namespaceUri().isEmpty()) {
					if (attribute.name() == QStringLiteral("fill") || attribute.name() == QStringLiteral("stroke"))
						value = paint(value);
					else if (attribute.name() == QStringLiteral("style")) value = css(value);
				}
				/* The memory-backed reader has no filename. Keep relative image
				 * references anchored to the source file when colors are rewritten. */
				if (reader.name() == QStringLiteral("image") && attribute.name() == QStringLiteral("href")) {
					const QUrl reference(value);
					if (reference.isRelative() && !value.startsWith('#')) value = url.resolved(reference).toLocalFile();
				}
				writer.writeAttribute(attribute.qualifiedName().toString(), value);
			}
			if (reader.name() == QStringLiteral("style")) {
				writer.writeCharacters(css(reader.readElementText()));
				writer.writeEndElement();
				--depth;
			}
		} else if (reader.isEndElement()) {
			if (depth == 1 && !paints.isEmpty()) {
				writer.writeStartElement(svgPrefix+"defs");
				for (int i = 0; i < paints.size(); ++i) {
					writer.writeStartElement(svgPrefix+"linearGradient");
					writer.writeAttribute("id", prefix+QString::number(i));
					writer.writeAttribute("gradientUnits", "userSpaceOnUse");
					for (const char *offset : {"0", "1"}) {
						writer.writeStartElement(svgPrefix+"stop");
						writer.writeAttribute("offset", QLatin1String(offset));
						writer.writeAttribute("stop-color", paints[i].name());
						writer.writeAttribute("stop-opacity", QString::number(paints[i].alphaF(), 'g', 8));
						writer.writeEndElement();
					}
					writer.writeEndElement();
				}
				writer.writeEndElement();
			}
			writer.writeEndElement();
			--depth;
		} else writer.writeCurrentToken(reader);
	}
	return changed && !reader.hasError() && !writer.hasError() ? output : QByteArray();
}
}

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
	const QByteArray format = reader.format();
	QBuffer normalized;
	if (format == "svg") {
		QFile source(url.toLocalFile());
		if (source.open(QIODevice::ReadOnly)) {
			const QByteArray colors = svgColorData(source.readAll(), url);
			if (!colors.isEmpty()) {
				normalized.setData(colors);
				normalized.open(QIODevice::ReadOnly);
				reader.setDevice(&normalized);
				reader.setFormat("svg");
			}
		}
	}
	QSize size = reader.size();
	/* Vector artwork is rendered at the requested resolution, including
	 * small intrinsic canvases. Scaling its original raster would blur it. */
	const bool vector = format == "svg" || format == "svgz";
	if (size.isValid() && (vector || size.width() > maxPixels || size.height() > maxPixels)) {
		size.scale(maxPixels, maxPixels, Qt::KeepAspectRatio);
		reader.setScaledSize(size);
	}
	return reader.read();
}
