#include "shortcut.h"

#include <windows.h>
#include <winuser.h>

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
	this->start();
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->wait();
}

void ShortcutPrivate::run()
{
	m_thread_id.store(static_cast<quintptr>(GetCurrentThreadId()));

	RegisterHotKey(NULL, 1, MOD_CONTROL, 0);

	MSG msg;
	::memset(&msg, 0, sizeof(MSG));

	while ((GetMessage(&msg, NULL, 0, 0) > 0) && !m_stoped.load()) {
		if (msg.message == WM_HOTKEY) {
			emit this->activated();
		}
	}

	/* Unregister on the thread that registered the hotkey */
	UnregisterHotKey(NULL, 1);
}

void ShortcutPrivate::stop()
{
	m_stoped.store(true);

	/* GetMessage() blocks until a message arrives: wake the loop so run()
	 * can return and wait() in the destructor does not hang */
	quintptr id = m_thread_id.load();
	if (id)
		PostThreadMessage(static_cast<DWORD>(id), WM_QUIT, 0, 0);
}
