#include "tests/testsupport.h"
#include "platform/pastetarget.h"
#include <QApplication>
#include <QClipboard>
#include <QProcess>
#include <QWidget>
#include <windows.h>
#include <cstdio>

namespace {
constexpr UINT busyMessage = WM_APP+1;
constexpr wchar_t busyProperty[] = L"PastesTestBusy";
constexpr wchar_t terminalProperty[] = L"PastesTestTerminal";
constexpr wchar_t shortcutProperty[] = L"PastesTestShortcut";

LRESULT CALLBACK editProcedure(HWND window, UINT message, WPARAM first, LPARAM second)
{
	const bool terminal = GetPropW(window, terminalProperty) != nullptr;
	if (message == WM_KEYDOWN && first == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
		SetPropW(GetParent(window), shortcutProperty, reinterpret_cast<HANDLE>(1));
		if (terminal) return 0;
	}
	// TranslateMessage can still generate the Ctrl+V character after keydown.
	if (terminal && message == WM_CHAR && first == 0x16) return 0;
	if (message == WM_KEYDOWN && first == VK_INSERT && (GetKeyState(VK_SHIFT) & 0x8000))
		SetPropW(GetParent(window), shortcutProperty, reinterpret_cast<HANDLE>(2));
	const auto original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_USERDATA));
	return CallWindowProcW(original, window, message, first, second);
}

LRESULT CALLBACK receiverProcedure(HWND window, UINT message, WPARAM first, LPARAM second)
{
	if (message == WM_CREATE) {
		const HWND edit = CreateWindowExW(0, L"EDIT", L"left|right",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 10, 260, 30,
			window, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(edit, EM_SETSEL, 5, 5);
		const auto original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(edit,
			GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(editProcedure)));
		SetWindowLongPtrW(edit, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(original));
		if (reinterpret_cast<CREATESTRUCTW *>(second)->lpCreateParams)
			SetPropW(edit, terminalProperty, reinterpret_cast<HANDLE>(1));
		return 0;
	}
	if (message == WM_ACTIVATE) {
		if (LOWORD(first) != WA_INACTIVE) SetFocus(GetDlgItem(window, 1));
		return 0;
	}
	if (message == busyMessage) {
		SetPropW(window, busyProperty, reinterpret_cast<HANDLE>(1));
		Sleep(600); // Simulate a target whose GUI thread is briefly busy.
		RemovePropW(window, busyProperty);
		return 0;
	}
	if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(window, message, first, second);
}

int receiver(bool terminal)
{
	WNDCLASSW type = {};
	type.lpfnWndProc = receiverProcedure;
	type.hInstance = GetModuleHandleW(nullptr);
	type.lpszClassName = L"PastesPasteTargetTest";
	RegisterClassW(&type);
	const HWND window = CreateWindowExW(0, type.lpszClassName, L"Pastes paste test",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 320, 140,
		nullptr, nullptr, type.hInstance, terminal ? reinterpret_cast<void *>(1) : nullptr);
	if (!window) return 1;
	std::printf("%llu\n", static_cast<unsigned long long>(reinterpret_cast<quintptr>(window)));
	std::fflush(stdout);
	MSG message = {};
	while (GetMessageW(&message, nullptr, 0, 0) > 0) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}
	return 0;
}

void activateWindow(HWND window)
{
	if (!SetForegroundWindow(window)) {
		// Give test-owned windows real input instead of bypassing focus rules.
		SetWindowPos(window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		RECT rectangle = {};
		GetWindowRect(window, &rectangle);
		INPUT input[3] = {};
		for (auto &event : input) event.type = INPUT_MOUSE;
		input[0].mi.dx = (rectangle.left+50-GetSystemMetrics(SM_XVIRTUALSCREEN))*65535 /
			(GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);
		input[0].mi.dy = (rectangle.top+12-GetSystemMetrics(SM_YVIRTUALSCREEN))*65535 /
			(GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
		input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
		input[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
		require(SendInput(3, input, sizeof(INPUT)) == 3, "Cannot click the native test window");
		waitUntil([&] { return GetForegroundWindow() == window; });
	}
	waitUntil([&] { return GetForegroundWindow() == window; });
	DWORD_PTR result = 0;
	require(SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG, 1000, &result) != 0,
		"Native test window did not finish activation");
}

class TestPanel final : public QWidget
{
public:
	TestPanel(void) { setGeometry(80, 80, 300, 120); }
};

class RestoreCursor final
{
public:
	RestoreCursor(void) { GetCursorPos(&m_position); }
	~RestoreCursor() { SetCursorPos(m_position.x, m_position.y); }
private:
	POINT m_position = {};
};

class Receiver final
{
public:
	explicit Receiver(bool terminal = false)
	{
		process.start(QCoreApplication::applicationFilePath(), {terminal ? "--terminal-receiver" : "--receiver"});
		require(process.waitForStarted() && process.waitForReadyRead(), "Receiver did not start");
		window = reinterpret_cast<HWND>(process.readLine().trimmed().toULongLong());
		require(IsWindow(window), "Receiver window missing");
	}
	~Receiver()
	{
		PostMessageW(window, WM_CLOSE, 0, 0);
		if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(); }
	}
	void activate(void)
	{
		activateWindow(window);
	}
	QString text(void) const
	{
		wchar_t text[256] = {};
		DWORD_PTR result = 0;
		if (!SendMessageTimeoutW(GetDlgItem(window, 1), WM_GETTEXT, 256,
			reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 50, &result)) return {};
		return QString::fromWCharArray(text);
	}
	QProcess process;
	HWND window = nullptr;
};

void settle(int duration)
{
	QEventLoop loop;
	QTimer::singleShot(duration, &loop, &QEventLoop::quit);
	loop.exec();
}

void directPaste(bool busy, int hideDelay = 200)
{
	Receiver receiver;
	TestPanel panel;
	panel.resize(300, 120);
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show();
	activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("PASTE_MARKER");
	if (busy) {
		PostMessageW(receiver.window, busyMessage, 0, 0);
		waitUntil([&] { return GetPropW(receiver.window, busyProperty) != nullptr; });
	}
	target.paste(&panel, false);
	QTimer::singleShot(hideDelay, &panel, &QWidget::hide);
	try { waitUntil([&] { return receiver.text() == "left|PASTE_MARKERright"; }); }
	catch (...) {
		qWarning() << "Native paste state" << busy << hideDelay << panel.isVisible()
			<< receiver.window << GetForegroundWindow() << receiver.text()
			<< GetPropW(receiver.window, shortcutProperty);
		throw;
	}
	settle(100);
	require(receiver.text() == "left|PASTE_MARKERright", "Paste repeated or lost the insertion point");
	require(GetPropW(receiver.window, shortcutProperty) == reinterpret_cast<HANDLE>(2),
		"Ordinary application did not receive Shift+Insert");
}

void cancelledPaste(void)
{
	Receiver receiver;
	TestPanel panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("CANCELLED_MARKER");
	target.paste(&panel, false);
	target.cancel();
	panel.hide();
	settle(800);
	if (receiver.text() != "left|right") qWarning() << "Cancelled receiver state" << receiver.text();
	require(receiver.text() == "left|right", "Cancelled request still pasted");
}

void switchedTarget(void)
{
	Receiver receiver;
	Receiver other;
	TestPanel panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("SWITCHED_MARKER");
	target.paste(&panel, false);
	other.activate();
	panel.hide();
	settle(800);
	require(receiver.text() == "left|right" && other.text() == "left|right" &&
		GetForegroundWindow() == other.window, "Pending paste stole focus after an application switch");
}

class HeldShift final
{
public:
	HeldShift(void) { send(false); }
	~HeldShift() { send(true); }
	void release(void) { send(true); }
private:
	void send(bool up)
	{
		INPUT input = {};
		input.type = INPUT_KEYBOARD;
		input.ki.wVk = VK_LSHIFT;
		input.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
		SendInput(1, &input, sizeof(INPUT));
	}
};

void waitsForModifierRelease(void)
{
	Receiver receiver;
	TestPanel panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("SHIFT_MARKER");
	HeldShift shift;
	waitUntil([] { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; });
	target.paste(&panel, false);
	panel.hide();
	settle(400);
	require(receiver.text() == "left|right", "Paste ran before the modifier was released");
	shift.release();
	waitUntil([&] { return receiver.text() == "left|SHIFT_MARKERright"; });
}

void reopeningCancelsPaste(void)
{
	Receiver receiver;
	TestPanel panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("REOPENED_MARKER");
	target.paste(&panel, false);
	// Reopen during the hide animation while the panel still owns focus.
	target.captureTarget(&panel);
	panel.hide();
	settle(800);
	require(receiver.text() == "left|right", "Reopening history left the old paste pending");
}

void terminalPaste(void)
{
	Receiver receiver(true);
	TestPanel panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); activateWindow(reinterpret_cast<HWND>(panel.winId()));
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("TERMINAL_MARKER");
	target.paste(&panel, false);
	panel.hide();
	waitUntil([&] { return receiver.text() == "left|TERMINAL_MARKERright"; });
	settle(100);
	require(receiver.text() == "left|TERMINAL_MARKERright", "Terminal paste repeated");
	require(GetPropW(receiver.window, shortcutProperty) == reinterpret_cast<HANDLE>(2),
		"Terminal did not receive Shift+Insert");
}
}

int main(int argc, char *argv[])
{
	if (argc > 1 && QByteArray(argv[1]) == "--receiver") return receiver(false);
	if (argc > 1 && QByteArray(argv[1]) == "--terminal-receiver") return receiver(true);
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	RestoreCursor cursor;
	return runTest("native paste at original insertion point", [] { directPaste(false); }) |
		runTest("native paste waits for another process to activate", [] { directPaste(true); }) |
		runTest("native paste waits for panel hiding", [] { directPaste(false, 600); }) |
		runTest("native paste cancellation", cancelledPaste) |
		runTest("native paste respects application switches", switchedTarget) |
		runTest("native paste waits for modifier release", waitsForModifierRelease) |
		runTest("native paste cancelled by reopening history", reopeningCancelsPaste) |
		runTest("native terminal paste uses Shift+Insert", terminalPaste);
}
