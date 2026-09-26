#include "shortcut.h"
#include "shortcut_win_state.h"

#include <windows.h>
#include <winuser.h>
#include <QDebug>

struct HookContext
{
	ShortcutPrivate *worker;
	WindowsShortcutState state;
};
static thread_local HookContext *currentHook = nullptr;

static LRESULT CALLBACK keyboardHook(int code, WPARAM message, LPARAM parameter)
{
	if (code == HC_ACTION && currentHook) {
		const auto *key = reinterpret_cast<const KBDLLHOOKSTRUCT *>(parameter);
		const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
		const bool up = message == WM_KEYUP || message == WM_SYSKEYUP;
		if (down || up) {
			const auto result = currentHook->state.keyEvent(key->vkCode, down, key->flags & LLKHF_INJECTED);
			if (result.open) {
				/* Win-down was forwarded. Mark its chord as used so releasing Win
				 * does not open Start after the swallowed V. Injected events are
				 * excluded from physical modifier state. No system settings change. */
				INPUT marker[2] = {};
				marker[0].type = marker[1].type = INPUT_KEYBOARD;
				marker[0].ki.wVk = marker[1].ki.wVk = VK_LCONTROL;
				marker[1].ki.dwFlags = KEYEVENTF_KEYUP;
				SendInput(2, marker, sizeof(INPUT));
				emit currentHook->worker->pasteActivated();
			}
			if (result.consume) return 1;
		}
	}
	return CallNextHookEx(nullptr, code, message, parameter);
}

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent) {}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->wait();
}

void ShortcutPrivate::run(void)
{
	/* Create the message queue before publishing its id, including early-stop. */
	MSG msg = {};
	PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);
	m_thread_id.store(static_cast<quintptr>(GetCurrentThreadId()));
	if (m_stoped.load()) { m_thread_id.store(0); return; }
	HookContext context{this, {}};
	for (unsigned key : {VK_LWIN, VK_RWIN, VK_LCONTROL, VK_RCONTROL,
			    VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU})
		context.state.seedModifier(key, (GetAsyncKeyState(key) & 0x8000) != 0);
	currentHook = &context;
	const HHOOK hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboardHook, GetModuleHandle(nullptr), 0);
	bool fallback = false;
	if (hook) {
		emit this->primaryShortcutChanged(QStringLiteral("Win+V"));
	} else {
		qWarning() << "Pastes: unable to install Win+V hook:" << GetLastError();
		fallback = RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'V');
		emit this->primaryShortcutChanged(fallback ? QStringLiteral("Ctrl+Shift+V") : QObject::tr("Tray icon"));
	}
	while (!m_stoped.load() && GetMessage(&msg, nullptr, 0, 0) > 0) {
		if (msg.message == WM_HOTKEY && msg.wParam == 2)
			emit this->pasteActivated();
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	if (hook) UnhookWindowsHookEx(hook);
	if (fallback) UnregisterHotKey(nullptr, 2);
	currentHook = nullptr;
	m_thread_id.store(0);
}

void ShortcutPrivate::stop(void)
{
	m_stoped.store(true);

	/* GetMessage() blocks until a message arrives: wake the loop so run()
	 * can return and wait() in the destructor does not hang */
	const quintptr id = m_thread_id.load();
	if (id)
		PostThreadMessage(static_cast<DWORD>(id), WM_QUIT, 0, 0);
}
