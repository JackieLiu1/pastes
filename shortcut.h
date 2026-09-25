#ifndef SHORTCUT_H
#define SHORTCUT_H

#include <QObject>
#include <QTimer>

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
	void activated(void);

protected:
	void run(void);

	/* atomic: written by the GUI thread, read by the hook thread */
	std::atomic_bool	m_stoped{false};
	/* platform thread id of run(), used to wake its blocking loop */
	std::atomic<quintptr>	m_thread_id{0};
};

class DoubleCtrlShortcut : public QObject
{
	Q_OBJECT

public:
	DoubleCtrlShortcut(QObject *parent = nullptr);
	~DoubleCtrlShortcut();

signals:
	void activated(void);

private:

	ShortcutPrivate		*m_shortcut;

	QTimer			*m_timer;
	bool			m_isActive;
};

#endif // SHORTCUT_H
