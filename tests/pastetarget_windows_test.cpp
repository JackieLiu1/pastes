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

LRESULT CALLBACK receiverProcedure(HWND window, UINT message, WPARAM first, LPARAM second)
{
	if (message == WM_CREATE) {
		const HWND edit = CreateWindowExW(0, L"EDIT", L"left|right",
			WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 10, 10, 260, 30,
			window, reinterpret_cast<HMENU>(1), GetModuleHandleW(nullptr), nullptr);
		SendMessageW(edit, EM_SETSEL, 5, 5);
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

int receiver(void)
{
	WNDCLASSW type = {};
	type.lpfnWndProc = receiverProcedure;
	type.hInstance = GetModuleHandleW(nullptr);
	type.lpszClassName = L"PastesPasteTargetTest";
	RegisterClassW(&type);
	const HWND window = CreateWindowExW(0, type.lpszClassName, L"Pastes paste test",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 320, 140,
		nullptr, nullptr, type.hInstance, nullptr);
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

class Receiver final
{
public:
	Receiver(void)
	{
		process.start(QCoreApplication::applicationFilePath(), {"--receiver"});
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
		SetForegroundWindow(window);
		waitUntil([&] { return GetForegroundWindow() == window; });
	}
	QString text(void) const
	{
		wchar_t text[256] = {};
		DWORD_PTR result = 0;
		SendMessageTimeoutW(GetDlgItem(window, 1), WM_GETTEXT, 256,
			reinterpret_cast<LPARAM>(text), SMTO_ABORTIFHUNG, 50, &result);
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
	QWidget panel;
	panel.resize(300, 120);
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show();
	panel.activateWindow();
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("PASTE_MARKER");
	if (busy) {
		PostMessageW(receiver.window, busyMessage, 0, 0);
		waitUntil([&] { return GetPropW(receiver.window, busyProperty) != nullptr; });
	}
	target.paste(&panel, false);
	QTimer::singleShot(hideDelay, &panel, &QWidget::hide);
	waitUntil([&] { return receiver.text() == "left|PASTE_MARKERright"; });
	settle(100);
	require(receiver.text() == "left|PASTE_MARKERright", "Paste repeated or lost the insertion point");
}

void cancelledPaste(void)
{
	Receiver receiver;
	QWidget panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); panel.activateWindow();
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("CANCELLED_MARKER");
	target.paste(&panel, false);
	target.cancel();
	panel.hide();
	settle(800);
	require(receiver.text() == "left|right", "Cancelled request still pasted");
}

void switchedTarget(void)
{
	Receiver receiver;
	Receiver other;
	QWidget panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); panel.activateWindow();
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
	QWidget panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); panel.activateWindow();
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
	QWidget panel;
	PasteTarget target;
	receiver.activate();
	target.captureTarget(&panel);
	panel.show(); panel.activateWindow();
	waitUntil([&] { return GetForegroundWindow() == reinterpret_cast<HWND>(panel.winId()); });
	QApplication::clipboard()->setText("REOPENED_MARKER");
	target.paste(&panel, false);
	// Reopen during the hide animation while the panel still owns focus.
	target.captureTarget(&panel);
	panel.hide();
	settle(800);
	require(receiver.text() == "left|right", "Reopening history left the old paste pending");
}
}

int main(int argc, char *argv[])
{
	if (argc > 1 && QByteArray(argv[1]) == "--receiver") return receiver();
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	return runTest("native paste at original insertion point", [] { directPaste(false); }) |
		runTest("native paste waits for another process to activate", [] { directPaste(true); }) |
		runTest("native paste waits for panel hiding", [] { directPaste(false, 600); }) |
		runTest("native paste cancellation", cancelledPaste) |
		runTest("native paste respects application switches", switchedTarget) |
		runTest("native paste waits for modifier release", waitsForModifierRelease) |
		runTest("native paste cancelled by reopening history", reopeningCancelsPaste);
}
