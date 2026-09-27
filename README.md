# Pastes

English | [简体中文](./README_cn.md)

![Release-CI](https://github.com/JackieLiu1/pastes/workflows/Release-CI/badge.svg) ![C/C++ CI](https://github.com/JackieLiu1/pastes/workflows/C/C++%20CI/badge.svg)

Thanks for choose pastes.

Pastes is a cross-platform clipboard manager. Open the history panel with `Win+V` on Windows, `Ctrl+Shift+V` on Linux, or the tray icon. While Pastes is running, it handles `Win+V`; exiting restores the native Windows behavior. If the Windows shortcut hook is unavailable, Pastes attempts `Ctrl+Shift+V` and displays the active shortcut in the panel and tray menu.

Pastes can hold the clipboard history within 7 days. You can add it to your system global clipboard by double-clicking or pressing `Enter`. What’s more surprising is that if the focused window can receive clipboard data, Then he will copy the data directly to the focus window.

Use `Tab` / `Shift+Tab` or the left/right arrow keys to cycle through visible cards. Press `Ctrl+F` to focus search, or start typing to search directly. `Tab` from search returns to the selected result.

Press `Space` to open a themed preview with selectable text and an image that fits the window. `Esc` or `Space` closes it; its buttons copy or paste the content.

Click a card to select it, double-click to paste, and drag horizontally to browse. Horizontal dragging follows your pointer and continues with a short glide after release; press again to stop it. Pulling or scrolling beyond either edge stretches the strip with increasing resistance, then springs it back. Wheel scrolling moves smoothly, and touchpad momentum retains its native distance. Drag a card upward until “Release to remove” appears, then let go to remove it. This requires about two thirds of its height (140–200 logical pixels). Dragging back or releasing below the threshold returns the card. The whole card follows your pointer beyond the panel. After release its upper edge tips backward into a bright line. Like an old TV switching off, the line contracts to a small central glow and fades out. The release animation takes about half a second. Small movements remain clicks.

Cards keep their source, type and time visible. Press `Space` to preview. `Delete` and the context menu remove an entry from history, leaving original files intact. Neighboring cards slide into the gap after removal. For eight seconds after deletion, click Undo or press `Ctrl+Z` to restore recent deletions one by one. A thin line unfolds into the card and settles back into its slot as neighboring cards slide aside. Undo during the fold reverses its current pose; undo after the fold starts from the line without replaying the shutdown flash.

![img](./pastes-view.gif)
