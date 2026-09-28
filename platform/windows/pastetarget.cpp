#include "platform/pastetarget.h"

#include <QTimer>
#include <QWidget>
#include <windows.h>

class PasteTarget::Private
{
public:
	quintptr target = 0;
};

PasteTarget::PasteTarget(QObject *parent) : QObject(parent),
	m_private(std::make_unique<Private>()) {}
PasteTarget::~PasteTarget() = default;

void PasteTarget::captureTarget(QWidget *panel)
{
	const HWND target = GetForegroundWindow();
	m_private->target = target && target != reinterpret_cast<HWND>(panel->winId())
		? reinterpret_cast<quintptr>(target) : 0;
}

void PasteTarget::paste(QWidget *panel, bool hasUrls)
{
	Q_UNUSED(panel);
	Q_UNUSED(hasUrls);
	const HWND target = reinterpret_cast<HWND>(m_private->target);
	if (!target || !IsWindow(target))
		return;
	DWORD owner = 0;
	GetWindowThreadProcessId(target, &owner);
	if (owner == GetCurrentProcessId())
		return;
	wchar_t className[64] = {};
	GetClassNameW(target, className, 64);
	if (lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
	    lstrcmpW(className, L"NotifyIconOverflowWindow") == 0)
		return;
	QTimer::singleShot(300, this, [target](void) {
		if (!IsWindow(target) || !SetForegroundWindow(target) || GetForegroundWindow() != target)
			return;
		INPUT input[4] = {};
		input[0].type = input[1].type = input[2].type = input[3].type = INPUT_KEYBOARD;
		input[0].ki.wVk = VK_CONTROL;
		input[1].ki.wVk = 'V';
		input[2].ki.wVk = 'V';
		input[2].ki.dwFlags = KEYEVENTF_KEYUP;
		input[3].ki.wVk = VK_CONTROL;
		input[3].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(4, input, sizeof(INPUT));
	});
}

void PasteTarget::cancel(void) {}
void PasteTarget::requestPermission(void) {}
