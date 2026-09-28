#include "platform/clipboardsource.h"

#include <QCache>
#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>
#import <AppKit/AppKit.h>

namespace {

QImage iconImage(NSImage *icon)
{
	if (!icon)
		return QImage();
	/* Keep enough pixels for source icons on Retina displays. */
	constexpr int pixels = 64;
	NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil
		pixelsWide:pixels pixelsHigh:pixels bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES
		isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
	if (!bitmap)
		return QImage();
	NSGraphicsContext *context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
	[NSGraphicsContext saveGraphicsState];
	[NSGraphicsContext setCurrentContext:context];
	CGContextClearRect(context.CGContext, CGRectMake(0, 0, pixels, pixels));
	[icon drawInRect:NSMakeRect(0, 0, pixels, pixels) fromRect:NSZeroRect
		operation:NSCompositingOperationCopy fraction:1.0 respectFlipped:NO
		hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
	[NSGraphicsContext restoreGraphicsState];
	NSData *png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
	const QImage image = QImage::fromData(static_cast<const uchar *>(png.bytes), int(png.length));
	[bitmap release];
	return image;
}

}

class ClipboardSource::Private
{
public:
	Private(void) : changeCount(NSPasteboard.generalPasteboard.changeCount)
	{
		setApplication(NSWorkspace.sharedWorkspace.frontmostApplication);
	}

	~Private()
	{
		[NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer];
	}

	void setApplication(NSRunningApplication *application)
	{
		/* Keep source identity after the outgoing process exits. */
		processId = application.processIdentifier;
		NSURL *url = application.bundleURL;
		if (!url && application.bundleIdentifier.length)
			url = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:application.bundleIdentifier];
		applicationPath = url ? QString::fromNSString(url.path) : QString();
	}

	QImage sourceIcon(NSPasteboard *pasteboard)
	{
		QString path;
		if ([pasteboard.types containsObject:@"org.nspasteboard.source"]) {
			NSString *bundle = [pasteboard stringForType:@"org.nspasteboard.source"];
			/* An explicit empty source means unknown, not the foreground app. */
			if (!bundle.length)
				return QImage();
			NSURL *url = [NSWorkspace.sharedWorkspace URLForApplicationWithBundleIdentifier:bundle];
			if (url)
				path = QString::fromNSString(url.path);
		} else {
			if ([pasteboard.types containsObject:@"com.apple.is-remote-clipboard"] ||
				processId == NSProcessInfo.processInfo.processIdentifier)
				return QImage();
			path = applicationPath;
		}
		if (path.isEmpty())
			return QImage();
		if (const QImage *cached = icons.object(path))
			return *cached;
		NSImage *nativeIcon = [NSWorkspace.sharedWorkspace iconForFile:path.toNSString()];
		const QImage image = iconImage(nativeIcon);
		if (!image.isNull())
			icons.insert(path, new QImage(image));
		return image;
	}

	NSInteger changeCount;
	pid_t processId = 0;
	QString applicationPath;
	id observer = nil;
	QImage icon;
	QCache<QString, QImage> icons{64};
};

ClipboardSource::ClipboardSource(QObject *parent) : ClipboardFeed(parent),
	m_private(std::make_unique<Private>())
{
	m_private->observer = [NSWorkspace.sharedWorkspace.notificationCenter
		addObserverForName:NSWorkspaceDidActivateApplicationNotification object:nil
		queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *notification) {
		/* Catch a copy followed immediately by an app switch before replacing
		 * the outgoing source, even if the polling timer has not fired yet. */
		this->checkClipboard();
		NSRunningApplication *application = notification.userInfo[NSWorkspaceApplicationKey];
		m_private->setApplication(application);
	}];
	auto *timer = new QTimer(this);
	timer->setInterval(100);
	QObject::connect(timer, &QTimer::timeout, this, &ClipboardSource::checkClipboard);
	timer->start();
	/* Qt reports our own writes and changes on activation immediately.
	 * The native counter deduplicates those events against background polls. */
	QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
		this, &ClipboardSource::checkClipboard);
}

ClipboardSource::~ClipboardSource() = default;

void ClipboardSource::checkClipboard(void)
{
	synchronize();
}

bool ClipboardSource::synchronize(void)
{
	NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
	const NSInteger count = pasteboard.changeCount;
	if (count == m_private->changeCount)
		return false;
	const QImage icon = m_private->sourceIcon(pasteboard);
	/* A promised pasteboard flavor may have changed while it was read. */
	if (pasteboard.changeCount != count)
		return true;
	m_private->changeCount = count;
	m_private->icon = icon;
	emit clipboardChanged();
	return true;
}

void ClipboardSource::capture(quint64 request)
{
	emit iconReady(request, m_private->icon);
}

int ClipboardSource::settleInterval(void) const
{
	return 0;
}

QImage ClipboardSource::snapshotIcon(void)
{
	return m_private->icon;
}
