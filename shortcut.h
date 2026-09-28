#ifndef SHORTCUT_H
#define SHORTCUT_H

#include <QObject>

#include <QThread>
#include <atomic>

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
#ifdef Q_OS_MACOS
	/* Carbon delivers hotkey events on the GUI thread. */
	void *m_event_handler = nullptr;
	void *m_hotkey = nullptr;
#endif
};

class GlobalShortcut : public QObject
{
	Q_OBJECT

public:
	GlobalShortcut(QObject *parent = nullptr);
	~GlobalShortcut();
	QString primaryShortcut(void) const;

signals:
	void pasteActivated(void);
	void primaryShortcutChanged(const QString &shortcut);

private:

	ShortcutPrivate		*m_shortcut;

};

#endif // SHORTCUT_H
