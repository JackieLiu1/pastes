#include <QDebug>
#include <QSocketNotifier>
#include <QTimer>

#include <X11/Xlib.h>
#include <X11/extensions/record.h>
#include <X11/Xlibint.h>
#include <X11/keysym.h>

#include "platform/shortcut_p.h"

class ShortcutPrivate::NativeState
{
public:
	explicit NativeState(ShortcutPrivate &worker) : m_worker(worker) {}
	~NativeState(void)
	{
		if (context) {
			if (enabled) XRecordDisableContext(control.get(), context);
			enabled = false;
			XRecordFreeContext(control.get(), context);
			XSync(control.get(), False);
			if (data) XRecordProcessReplies(data.get());
		}
	}

	static void callback(XPointer ptr, XRecordInterceptData *raw)
	{
		std::unique_ptr<XRecordInterceptData, decltype(&XRecordFreeData)> record(raw, &XRecordFreeData);
		auto &state = *reinterpret_cast<NativeState *>(ptr);
		if (!state.enabled || record->category != XRecordFromServer ||
			record->data_len < sizeof(xEvent)/4) return;
		const auto *event = reinterpret_cast<const xEvent *>(record->data);
		if (event->u.u.type == KeyPress && event->u.u.detail == state.pasteKey &&
			(event->u.keyButtonPointer.state & (ControlMask | ShiftMask)) == (ControlMask | ShiftMask))
			emit state.m_worker.pasteActivated();
	}

	std::unique_ptr<Display, decltype(&XCloseDisplay)> control{nullptr, &XCloseDisplay};
	std::unique_ptr<Display, decltype(&XCloseDisplay)> data{nullptr, &XCloseDisplay};
	XRecordContext context = 0;
	KeyCode pasteKey = 0;
	bool enabled = false;

private:
	ShortcutPrivate &m_worker;
};

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->wait();
}

void ShortcutPrivate::run(void)
{
	if (m_stoped.load()) return;
	/* Both X11 connections and their context stay on this worker thread. */
	NativeState state(*this);
	state.control.reset(XOpenDisplay(nullptr));
	state.data.reset(XOpenDisplay(nullptr));
	int major = 0, minor = 0;
	if (!state.control || !state.data ||
		!XRecordQueryVersion(state.control.get(), &major, &minor)) {
		qWarning() << "Pastes: unable to open X11 RECORD connections";
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	state.pasteKey = XKeysymToKeycode(state.control.get(), XK_v);
	std::unique_ptr<XRecordRange, decltype(&XFree)> range(XRecordAllocRange(), &XFree);
	if (!range || !state.pasteKey) {
		qWarning() << "Pastes: unable to allocate X11 shortcut range";
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	*range = {};
	range->device_events.first = KeyPress;
	range->device_events.last = KeyPress;
	XRecordRange *ranges[] = {range.get()};
	XRecordClientSpec clients = XRecordAllClients;
	state.context = XRecordCreateContext(state.control.get(), 0, &clients, 1, ranges, 1);
	XSync(state.control.get(), False);
	if (state.context)
		state.enabled = XRecordEnableContextAsync(state.data.get(), state.context,
			&NativeState::callback, reinterpret_cast<XPointer>(&state));
	if (!state.enabled) {
		qWarning() << "Pastes: unable to enable X11 shortcut recording";
		emit primaryShortcutChanged(QObject::tr("Tray icon"));
		return;
	}
	emit primaryShortcutChanged(QStringLiteral("Ctrl+Shift+V"));
	QSocketNotifier notifier(ConnectionNumber(state.data.get()), QSocketNotifier::Read);
	connect(&notifier, &QSocketNotifier::activated, &notifier, [this, &state] {
		if (!m_stoped.load()) XRecordProcessReplies(state.data.get());
	});
	/* Also handle a stop requested just before the event loop starts. */
	QTimer::singleShot(0, &notifier, [this] { if (m_stoped.load()) quit(); });
	if (!m_stoped.load()) exec();
}

void ShortcutPrivate::stop(void)
{
	m_stoped.store(true);
	/* quit() wakes the worker event loop; native cleanup follows on that thread. */
	quit();
}

QString GlobalShortcut::primaryShortcut(void) const
{
	return QStringLiteral("Ctrl+Shift+V");
}
