#include "shortcut.h"

#include <QtWin>
#include <winuser.h>

bool stoped;

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
	this->start();
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
}

void ShortcutPrivate::run()
{
	RegisterHotKey(NULL, 1, MOD_CONTROL, 0);

	MSG msg;
	::memset(&msg, 0, sizeof(MSG));

	while ((GetMessage(&msg, NULL, 0, 0) != 0) && !stoped) {
		if (msg.message == WM_HOTKEY) {
			emit this->activated();
		}
	}
}

void ShortcutPrivate::stop()
{
	stoped = true;
	UnregisterHotKey(NULL, 1);
}
