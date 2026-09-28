#ifndef PLATFORM_SHORTCUT_P_H
#define PLATFORM_SHORTCUT_P_H

#include "platform/globalshortcut.h"
#include <QThread>
#include <atomic>
#include <memory>

/* Backend worker details are deliberately absent from the public facade. */
class ShortcutPrivate : public QThread
{
	Q_OBJECT
public:
	ShortcutPrivate(QObject *parent = nullptr);
	~ShortcutPrivate();

	void stop(void);

Q_SIGNALS:
	void pasteActivated(void);
	void primaryShortcutChanged(const QString &shortcut);

protected:
	void run(void);

	/* atomic: written by the GUI thread, read by the hook thread */
	std::atomic_bool	m_stoped{false};
	/* platform thread id of run(), used to wake its blocking loop */
	std::atomic<quintptr>	m_thread_id{0};
	class NativeState;
	std::unique_ptr<NativeState> m_native;
};

#endif
