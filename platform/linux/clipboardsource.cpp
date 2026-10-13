#include "platform/clipboardsource.h"
#include "platform/linux/clipboardicon.h"
#include "platform/diagnosticlog.h"
#include "core/sourceicon.h"

#include <QApplication>
#include <QCache>
#include <QClipboard>
#include <QDir>
#include <QIcon>
#include <QJsonArray>
#include <QMimeData>
#include <QThread>

class ClipboardSource::Private
{
public:
	quint64 changes = 0;
	quint64 request = 0;
	QImage icon;
	QObject *worker = new QObject;
	QThread thread;
	QCache<QString, QString> desktopIcons{64};
	QCache<QString, QImage> images{64};
	Platform::DiagnosticLog diagnostics{Platform::DiagnosticLog::clipboardPath()};
};

ClipboardSource::ClipboardSource(QObject *parent) : ClipboardFeed(parent),
	m_private(std::make_unique<Private>())
{
	m_private->worker->moveToThread(&m_private->thread);
	QObject::connect(&m_private->thread, &QThread::finished, m_private->worker, &QObject::deleteLater);
	m_private->thread.start();
	QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged,
		this, [this] {
			++m_private->changes;
			emit clipboardChanged();
		});
	m_private->diagnostics.append({{"backend", "linux"}, {"event", "session-start"}});
}

ClipboardSource::~ClipboardSource()
{
	m_private->thread.requestInterruption();
	m_private->thread.quit();
	m_private->thread.wait();
}

int ClipboardSource::settleInterval(void) const
{
	return 1000;
}

bool ClipboardSource::synchronize(void)
{
	return false;
}

bool ClipboardSource::allowsCapture(void) const
{
	/* Qt asks X11 for format declarations, without retrieving their data. */
	const QMimeData *mime = QApplication::clipboard()->mimeData();
	const QStringList formats = mime ? mime->formats() : QStringList();
	QJsonArray types;
	for (const QString &format : formats.mid(0, 32)) types.append(format.left(128));
	m_private->diagnostics.append({{"backend", "linux"},
		{"changeCount", qint64(m_private->changes)}, {"captureAllowed", true},
		{"types", types}, {"typeCount", formats.size()}});
	return true;
}

void ClipboardSource::capture(quint64 request)
{
	const auto source = LinuxClipboardIcon::captureSource();
	m_private->request = request;
	m_private->icon = source.icon;
	m_private->diagnostics.append({{"backend", "linux"}, {"event", "source-capture"},
		{"request", qint64(request)}, {"owner", qint64(source.owner)},
		{"sourceProcessId", qint64(source.processId)}, {"windowIcon", !source.icon.isNull()}});
	if (!source.icon.isNull() || (source.classes.isEmpty() && source.desktopIds.isEmpty() && source.executable.isEmpty())) {
		emit iconReady(request, source.icon);
		return;
	}
	QMetaObject::invokeMethod(m_private->worker, [this, source, request](void) {
		if (m_private->thread.isInterruptionRequested()) return;
		const QString key = source.desktopIds.join('\n')+'\n'+source.classes.join('\n')+'\n'+source.executable;
		QString name;
		if (const QString *cached = m_private->desktopIcons.object(key)) name = *cached;
		else {
			name = LinuxClipboardIcon::desktopIcon(source);
			if (!name.isEmpty()) m_private->desktopIcons.insert(key, new QString(name));
		}
		if (m_private->thread.isInterruptionRequested()) return;
		QMetaObject::invokeMethod(this, [this, request, name](void) {
			QImage image;
			if (const QImage *cached = m_private->images.object(name)) image = *cached;
			else if (!name.isEmpty()) {
				/* QIcon theme engines and QPixmap stay on the GUI thread. */
				const QIcon icon = QDir::isAbsolutePath(name) ? QIcon(name) : QIcon::fromTheme(name);
				image = SourceIcon::bounded(icon.pixmap(SourceIcon::maxPixels, SourceIcon::maxPixels).toImage());
				if (!image.isNull()) m_private->images.insert(name, new QImage(image));
			}
			if (request == m_private->request) m_private->icon = image;
			emit iconReady(request, image);
		}, Qt::QueuedConnection);
	}, Qt::QueuedConnection);
}

QImage ClipboardSource::snapshotIcon(void)
{
	return m_private->icon;
}

bool ClipboardSource::hasImageContent(void) const
{
	return true;
}
