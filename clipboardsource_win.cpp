#include "clipboardsource_win.h"

#include <windows.h>
#include <shellapi.h>

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
	const QImage generic = QImage::fromHICON(defaultIcon)
		.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	QImage image = nativeImage.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	return image == generic ? QImage() : image;
}

QImage windowIcon(HWND window, DWORD processId, bool allSizes = false)
{
	DWORD actualId = 0;
	if (!window || !GetWindowThreadProcessId(window, &actualId) || actualId != processId)
		return QImage();
	const WPARAM sizes[] = {ICON_SMALL2, ICON_SMALL, ICON_BIG};
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
	QImage image = iconImage(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICONSM)));
	if (image.isNull())
		image = iconImage(reinterpret_cast<HICON>(GetClassLongPtrW(window, GCLP_HICON)));
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
	if (source.icon.isNull()) {
		HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, source.processId);
		if (process) {
			wchar_t filename[32768];
			DWORD size = sizeof(filename)/sizeof(filename[0]);
			if (QueryFullProcessImageNameW(process, 0, filename, &size))
				source.executable = QString::fromWCharArray(filename, size);
			CloseHandle(process);
		}
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
	HICON large = nullptr, small = nullptr;
	/* Read the real executable resource, not an icon inferred from .exe. */
	ExtractIconExW(reinterpret_cast<const wchar_t *>(path.utf16()), 0, &large, &small, 1);
	QImage image = iconImage(large ? large : small);
	if (large)
		DestroyIcon(large);
	if (small)
		DestroyIcon(small);
	return image;
}

}

ClipboardSource::ClipboardSource(QObject *parent) : QObject(parent), m_worker(new QObject)
{
	m_worker->moveToThread(&m_thread);
	QObject::connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
	m_thread.start();
}

ClipboardSource::~ClipboardSource()
{
	m_thread.requestInterruption();
	m_thread.quit();
	m_thread.wait();
}

void ClipboardSource::capture(quint64 request)
{
	const Source source = captureSource();
	QMetaObject::invokeMethod(m_worker, [this, source, request](void) {
		if (m_thread.isInterruptionRequested())
			return;
		QImage image = source.icon;
		if (image.isNull() && source.processId) {
			WindowSearch search{source.processId, QImage()};
			EnumWindows(findSourceWindow, reinterpret_cast<LPARAM>(&search));
			image = search.icon;
		}
		if (image.isNull() && !source.executable.isEmpty()) {
			if (const QImage *cached = m_icons.object(source.executable))
				image = *cached;
			else {
				image = executableIcon(source.executable);
				if (!image.isNull())
					m_icons.insert(source.executable, new QImage(image));
			}
		}
		emit iconReady(request, image);
	}, Qt::QueuedConnection);
}
