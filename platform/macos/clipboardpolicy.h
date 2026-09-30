#ifndef PASTES_MACOS_CLIPBOARDPOLICY_H
#define PASTES_MACOS_CLIPBOARDPOLICY_H

@class NSPasteboard;

namespace Platform {
/* Read type declarations only, including markers Qt does not expose. */
bool allowsClipboardHistory(NSPasteboard *pasteboard);
bool hasClipboardImageContent(NSPasteboard *pasteboard);
}

#endif
