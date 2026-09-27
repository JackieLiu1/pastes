#include "shortcut.h"

#include <Carbon/Carbon.h>
#include <QDebug>
#include <QTimer>

static OSStatus handleHotkey(EventHandlerCallRef, EventRef event, void *context)
{
	EventHotKeyID key = {};
	if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
			      nullptr, sizeof(key), nullptr, &key) != noErr ||
	    key.signature != 0x50737473 || key.id != 1)
		return eventNotHandledErr;
	auto *worker = static_cast<ShortcutPrivate *>(context);
	qInfo() << "Pastes: Shift+Cmd+V pressed";
	/* Finish native event dispatch before activating a Qt window. */
	QTimer::singleShot(0, worker, [worker](void) {
		emit worker->pasteActivated();
	});
	return noErr;
}

ShortcutPrivate::ShortcutPrivate(QObject *parent) : QThread(parent)
{
	/* Publish after GlobalShortcut and MainWindow connect their signals. */
	QTimer::singleShot(0, this, [this](void) {
		emit this->primaryShortcutChanged(m_hotkey ? QStringLiteral("Shift+Cmd+V")
							 : QObject::tr("Tray icon"));
	});
	const EventTypeSpec type = {kEventClassKeyboard, kEventHotKeyPressed};
	EventHandlerRef handler = nullptr;
	OSStatus status = InstallApplicationEventHandler(&handleHotkey, 1, &type,
							this, &handler);
	if (status != noErr) {
		qWarning() << "Pastes: unable to install hotkey handler:" << status;
		return;
	}
	m_event_handler = handler;
	const EventHotKeyID key = {0x50737473, 1};
	EventHotKeyRef hotkey = nullptr;
	status = RegisterEventHotKey(kVK_ANSI_V, cmdKey | shiftKey, key,
				    GetApplicationEventTarget(), 0, &hotkey);
	if (status == noErr) {
		m_hotkey = hotkey;
		qInfo() << "Pastes: registered Shift+Cmd+V";
	} else {
		qWarning() << "Pastes: unable to register Shift+Cmd+V:" << status;
	}
}

ShortcutPrivate::~ShortcutPrivate()
{
	this->stop();
	this->wait();
}

void ShortcutPrivate::run(void)
{
	/* Carbon's application event target uses the existing GUI event loop. */
}

void ShortcutPrivate::stop(void)
{
	/* Registration and teardown stay on the GUI thread. */
	if (m_hotkey) {
		UnregisterEventHotKey(static_cast<EventHotKeyRef>(m_hotkey));
		m_hotkey = nullptr;
	}
	if (m_event_handler) {
		RemoveEventHandler(static_cast<EventHandlerRef>(m_event_handler));
		m_event_handler = nullptr;
	}
}
