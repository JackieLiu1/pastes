#ifndef PASTES_ENTRY_FIXTURES_H
#define PASTES_ENTRY_FIXTURES_H

#include "tests/support/testsupport.h"
#include "core/clipboardcontent.h"
#include "core/itemdata.h"
#include <QBuffer>
#include <QColorSpace>

inline HistoryEntry textEntry(const QString &text, QDateTime time = QDateTime::currentDateTime())
{
	auto entry = HistoryEntry::create();
	entry->mimeData = std::make_unique<QMimeData>();
	entry->mimeData->setText(text);
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	entry->time = time;
	return entry;
}

inline EventVersion nextVersion(EventVersion previous, const QString &device = "test-peer")
{
	++previous.logical;
	previous.device = device;
	return previous;
}
inline void observeIcon(const HistoryEntry &entry, const QImage &icon, EventVersion version = {})
{
	if (!version.valid())
		version = entry->events.copy.valid()
			? entry->events.copy
			: EventVersion{entry->time.toMSecsSinceEpoch(), 0, "test-source"};
	entry->events.source = version;
	entry->events.sourceCaptured = entry->time.toMSecsSinceEpoch();
	entry->icon = icon;
}

inline QByteArray png(const QImage &image)
{
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	require(image.save(&buffer, "PNG"), "PNG encoding failed");
	return bytes;
}

inline QImage colorSpaceImage(bool srgb = false)
{
	QImage image(24, 16, QImage::Format_RGBA8888);
	image.setColorSpace(QColorSpace::DisplayP3);
	for (int y = 0; y < image.height(); ++y) {
		auto *row = image.scanLine(y);
		for (int x = 0; x < image.width(); ++x) {
			row[x*4] = 30+x*7;
			row[x*4+1] = 40+y*9;
			row[x*4+2] = 60+(x+y)*3;
			row[x*4+3] = 255;
		}
	}
	if (srgb) image.convertToColorSpace(QColorSpace::SRgb);
	return image;
}

#endif
