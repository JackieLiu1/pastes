#include "platform/pastetarget.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <windows.h>

Q_LOGGING_CATEGORY(pasteLog, "pastes.paste", QtWarningMsg)

class PasteTarget::Private
{
public:
	HWND target = nullptr;
	DWORD processId = 0;
	QPointer<QWidget> panel;
	QTimer timer;
	QElapsedTimer elapsed;
	bool activationRequested = false;
};

PasteTarget::PasteTarget(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>())
{
	m_private->timer.setInterval(20);
	QObject::connect(&m_private->timer, &QTimer::timeout, this, &PasteTarget::tryPaste);
}

PasteTarget::~PasteTarget() = default;

void PasteTarget::captureTarget(QWidget *panel)
{
	Q_UNUSED(panel);
	cancel();
	const HWND target = GetForegroundWindow();
	DWORD owner = 0;
	GetWindowThreadProcessId(target, &owner);
	m_private->target = owner && owner != GetCurrentProcessId() ? target : nullptr;
	m_private->processId = owner;
}

void PasteTarget::paste(QWidget *panel, bool hasUrls)
{
	Q_UNUSED(hasUrls);
	cancel();
	const HWND target = m_private->target;
	if (!panel || !target || !IsWindow(target))
		return;
	wchar_t className[64] = {};
	GetClassNameW(target, className, 64);
	if (lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
	    lstrcmpW(className, L"NotifyIconOverflowWindow") == 0)
		return;
	m_private->panel = panel;
	m_private->activationRequested = false;
	m_private->elapsed.start();
	m_private->timer.start();
}

void PasteTarget::tryPaste(void)
{
	const HWND target = m_private->target;
	DWORD owner = 0;
	const DWORD thread = GetWindowThreadProcessId(target, &owner);
	if (!m_private->panel || !IsWindow(target) || !thread || owner != m_private->processId) {
		cancel();
		return;
	}
	if (m_private->elapsed.elapsed() > 1500) {
		qCWarning(pasteLog) << "Timed out waiting for paste target activation";
		cancel();
		return;
	}
	/* Wait for the hide animation and physical shortcut/click to finish. */
	if (m_private->panel->isVisible())
		return;
	const HWND foreground = GetForegroundWindow();
	DWORD foregroundOwner = 0;
	GetWindowThreadProcessId(foreground, &foregroundOwner);
	if (foreground && foreground != target && foregroundOwner != GetCurrentProcessId()) {
		cancel(); // Respect an application switch while a paste is pending.
		return;
	}
	for (int key : {VK_LWIN, VK_RWIN, VK_CONTROL, VK_SHIFT, VK_MENU, VK_LBUTTON, VK_RBUTTON})
		if (GetAsyncKeyState(key) & 0x8000) return;
	if (!m_private->activationRequested) {
		m_private->activationRequested = true;
		if (foreground != target) SetForegroundWindow(target);
		/* Cross-process activation is asynchronous, including on success. */
		return;
	}
	GUITHREADINFO focus = {};
	focus.cbSize = sizeof(focus);
	if (foreground != target || !GetGUIThreadInfo(thread, &focus) || !focus.hwndFocus)
		return;
	cancel();
	INPUT input[4] = {};
	input[0].type = input[1].type = input[2].type = input[3].type = INPUT_KEYBOARD;
	input[0].ki.wVk = VK_CONTROL;
	input[1].ki.wVk = 'V';
	input[2].ki.wVk = 'V';
	input[2].ki.dwFlags = KEYEVENTF_KEYUP;
	input[3].ki.wVk = VK_CONTROL;
	input[3].ki.dwFlags = KEYEVENTF_KEYUP;
	if (SendInput(4, input, sizeof(INPUT)) != 4)
		qCWarning(pasteLog) << "Unable to send paste shortcut:" << GetLastError();
}

void PasteTarget::cancel(void)
{
	m_private->timer.stop();
	m_private->panel.clear();
}

void PasteTarget::requestPermission(void) {}
QString PasteTarget::permissionRecoveryHint(void) { return {}; }
