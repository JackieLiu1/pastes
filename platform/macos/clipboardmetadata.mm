#include "platform/macos/clipboardmetadata.h"

#include <QJsonArray>
#import <AppKit/AppKit.h>

namespace {
QJsonArray typeNames(NSArray<NSPasteboardType> *types)
{
	QJsonArray names;
	for (NSPasteboardType type in types) {
		if (names.size() == 32) break;
		names.append(QString::fromNSString(type).left(128));
	}
	return names;
}
}

QJsonObject Platform::clipboardMetadata(NSPasteboard *pasteboard, bool allowed, const QString &foreground)
{
	QJsonArray items;
	NSArray<NSPasteboardItem *> *nativeItems = pasteboard.pasteboardItems;
	for (NSPasteboardItem *item in nativeItems) {
		if (items.size() == 16) break;
		items.append(QJsonObject{{"types", typeNames(item.types)}, {"typeCount", int(item.types.count)}});
	}
	return {{"backend", "macos"}, {"changeCount", qint64(pasteboard.changeCount)},
		{"captureAllowed", allowed}, {"foregroundApp", foreground.left(256)},
		{"types", typeNames(pasteboard.types)}, {"typeCount", int(pasteboard.types.count)},
		{"items", items}, {"itemCount", int(nativeItems.count)}};
}
