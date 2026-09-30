#include "platform/clipboardsource.h"
#include "core/sourceicon.h"

#include <QApplication>
#include <QClipboard>
#include <QThread>
#include <QCache>
#include <windows.h>
#include <shlobj.h>

namespace {

QImage iconImage(HICON icon)
{
	if (!icon)
		return QImage();
	const QImage nativeImage = QImage::fromHICON(icon);
	if (nativeImage.isNull())
		return QImage();
	/* A default window-class icon conveys no source identity. */
	HICON defaultIcon = reinterpret_cast<HICON>(LoadImageW(nullptr, MAKEINTRESOURCEW(32512),
		IMAGE_ICON, nativeImage.width(), nativeImage.height(), LR_SHARED));
	const QImage generic = QImage::fromHICON(defaultIcon);
	return nativeImage == generic ? QImage() : SourceIcon::bounded(nativeImage);
}

QImage windowIcon(HWND window, DWORD processId, bool allSizes = false)
{
	DWORD actualId = 0;
	if (!window || !GetWindowThreadProcessId(window, &actualId) || actualId != processId)
		return QImage();
	const WPARAM sizes[] = {ICON_BIG, ICON_SMALL, ICON_SMALL2};
	for (int i = 0; i < (allSizes ? 3 : 1); ++i) {
		DWORD_PTR result = 0;
		/* Never block the clipboard listener indefinitely on another app. */
		if (SendMessageTimeoutW(window, WM_GETICON, sizes[i], 0,
			SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 35, &result)) {
			QImage image = iconImage(reinterpret_cast<HICON>(result));
			if (!image.isNull())
				return image;
		}
	}
	QImage image = iconImage(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON)));
	if (image.isNull())
		image = iconImage(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM)));
	return image;
}

struct Source
{
	DWORD processId = 0;
	QString executable;
	QImage icon;
};

Source captureSource(void)
{
	Source source;
	HWND owner = GetClipboardOwner();
	if (!owner || !GetWindowThreadProcessId(owner, &source.processId))
		return source;
	/* Copy the icon now: helper windows can disappear before the 1s debounce. */
	source.icon = windowIcon(owner, source.processId);
	HWND foreground = GetForegroundWindow();
	DWORD foregroundId = 0;
	GetWindowThreadProcessId(foreground, &foregroundId);
	if (foregroundId == source.processId) {
		if (source.icon.isNull() && foreground != owner)
			source.icon = windowIcon(foreground, source.processId);
	}
	/* A usable caption icon can still be only 16 px. Always retain the path
	 * so the worker can request a larger executable resource. */
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, source.processId);
	if (process) {
		wchar_t filename[32768];
		DWORD size = sizeof(filename)/sizeof(filename[0]);
		if (QueryFullProcessImageNameW(process, 0, filename, &size))
			source.executable = QString::fromWCharArray(filename, size);
		CloseHandle(process);
	}
	return source;
}

struct WindowSearch
{
	DWORD processId;
	QImage icon;
	int remaining = 8;
};

BOOL CALLBACK findSourceWindow(HWND window, LPARAM param)
{
	auto *search = reinterpret_cast<WindowSearch *>(param);
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != search->processId || !IsWindowVisible(window))
		return TRUE;
	search->icon = windowIcon(window, search->processId, true);
	return search->icon.isNull() && --search->remaining > 0;
}

QImage executableIcon(const QString &path)
{
	if (path.isEmpty())
		return QImage();
	HICON icon = nullptr;
	/* ExtractIconEx uses the system's small/large metrics (often 16/32 px).
	 * Ask the shell for a HiDPI resource instead, on the worker thread. */
	SHDefExtractIconW(reinterpret_cast<const wchar_t *>(path.utf16()), 0, 0,
		&icon, nullptr, SourceIcon::maxPixels);
	QImage image = iconImage(icon);
	if (icon)
		DestroyIcon(icon);
	return image;
}

}

class ClipboardSource::Private
{
public:
	QObject *worker = new QObject;
	QThread thread;
	QCache<QString, QImage> icons{64};
};

ClipboardSource::ClipboardSource(QObject *parent) : ClipboardFeed(parent),
	m_private(std::make_unique<Private>())
{
	m_private->worker->moveToThread(&m_private->thread);
	QObject::connect(&m_private->thread, &QThread::finished, m_private->worker, &QObject::deleteLater);
	m_private->thread.start();
	QObject::connect(QApplication::clipboard(), &QClipboard::dataChanged,
		this, &ClipboardSource::clipboardChanged);
}

ClipboardSource::~ClipboardSource()
{
	m_private->thread.requestInterruption();
	m_private->thread.quit();
	m_private->thread.wait();
}

void ClipboardSource::capture(quint64 request)
{
	const Source source = captureSource();
	QMetaObject::invokeMethod(m_private->worker, [this, source, request](void) {
		if (m_private->thread.isInterruptionRequested())
			return;
		QImage image;
		if (!source.executable.isEmpty()) {
			if (const QImage *cached = m_private->icons.object(source.executable))
				image = *cached;
			else {
				image = executableIcon(source.executable);
				if (!image.isNull())
					m_private->icons.insert(source.executable, new QImage(image));
			}
		}
		if (image.isNull())
			image = source.icon;
		if (image.isNull() && source.processId) {
			WindowSearch search{source.processId, QImage()};
			EnumWindows(findSourceWindow, reinterpret_cast<LPARAM>(&search));
			image = search.icon;
		}
		emit iconReady(request, image);
	}, Qt::QueuedConnection);
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
	return true;
}

QImage ClipboardSource::snapshotIcon(void)
{
	return QImage();
}
