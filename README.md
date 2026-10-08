# Pastes

English | [简体中文](./README_cn.md)

For macOS installers, see [the packaging guide](packaging/macos/README.md).
For Windows installers, see [the packaging guide](packaging/windows/README.md).
For Linux installers, see [the packaging guide](packaging/linux/README.md).
All platforms use `cmake --build build --target package`; packages are written
to the build directory's `dist/` folder.

![Release-CI](https://github.com/JackieLiu1/pastes/workflows/Release-CI/badge.svg) ![C/C++ CI](https://github.com/JackieLiu1/pastes/workflows/C/C++%20CI/badge.svg)

Thanks for choose pastes.

Pastes is a cross-platform clipboard manager. Open the history panel with `Win+V` on Windows and Linux, or the tray icon. While Pastes is running on Windows, it handles `Win+V`; exiting restores the native Windows behavior. If the Windows shortcut hook is unavailable, Pastes attempts `Ctrl+Shift+V` and displays the active shortcut in the panel and tray menu.

Pastes keeps the last 30 days of clipboard history. You can add it to your system global clipboard by double-clicking or pressing `Enter`. What’s more surprising is that if the focused window can receive clipboard data, Then he will copy the data directly to the focus window.

Use `Tab` / `Shift+Tab` or the left/right arrow keys to cycle through visible cards. Press `Ctrl+F` to focus search, or start typing to search directly. `Tab` from search returns to the selected result.

Press `Space` to open a themed preview with selectable text and an image that fits the window. `Esc` or `Space` closes it; its buttons copy or paste the content.

The history panel stays visible while previewing. About and Settings hide the panel; close those dialogs, then invoke Pastes again to return to history.

SVG files show their artwork when their cards become visible. Thumbnails and full previews render at the display resolution; copy and paste still use the original file. Missing or unreadable SVG files retain their file representation.

Click a card to select it, double-click to paste, and drag horizontally to browse. Horizontal dragging follows your pointer and continues with a short glide after release; press again to stop it. Pulling or scrolling beyond either edge stretches the strip with increasing resistance, then springs it back. Wheel scrolling moves smoothly, and touchpad momentum retains its native distance. Drag a card upward until “Release to remove” appears, then let go to remove it. This requires about two thirds of its height (140–200 logical pixels). Dragging back or releasing below the threshold returns the card. The whole card follows your pointer beyond the panel. After release its upper edge tips backward into a bright line. Like an old TV switching off, the line contracts to a small central glow, emits a brief burst of sparks and fades out. The release animation takes about half a second. Small movements remain clicks.

Cards keep their source, type and time visible. Press `Space` to preview. `Delete` and the context menu remove an entry from history, leaving original files intact. Neighboring cards slide into the gap after removal. For eight seconds after deletion, click Undo or press `Ctrl+Z` to restore recent deletions one by one. A thin line unfolds into the card and settles back into its slot as neighboring cards slide aside. Undo during the fold reverses its current pose; undo after the fold starts from the line without replaying the shutdown flash or sparks.

![img](./pastes-view.gif)

## Settings

Open Settings from the panel or tray menu. Changes apply immediately and
persist across restarts:

- Choose a light or dark theme for the panel and its dialogs.
- Show or hide keyboard hints below the cards.
- Launch at sign-in using a current-user Windows startup entry or a Linux
  user autostart entry. Toggle startup off and on after moving the executable.
- Pause clipboard recording without removing existing history. Pending
  capture is cancelled; resuming records only subsequent copies. The panel
  and tray tooltip show when recording is paused.
- Read the active open shortcut and the paste, preview and search shortcuts.

About Pastes shows the application icon, build version, author, license and
project link in a themed dialog. History remains local with a 30-day
retention period.

## Platform architecture

The project requires C++17 and Qt 6.3 or newer. `core/` owns data and rules,
`application/` owns use cases and ports, `storage/` implements SQLite, and
`ui/` presents history. `app/` assembles their concrete dependencies.
See [the architecture and testing guide](docs/architecture.md).

UI and clipboard adapters use public C++/Qt contracts in `platform/`. Native
clipboard, paste, window, shortcut, icon, path and startup implementations
live in `platform/windows/`, `platform/macos/` and `platform/linux/`. CMake
selects one backend; Windows and Linux reuse Qt behavior from
`platform/desktop/`. See [the interface and ownership guide](docs/platform-architecture.md).
