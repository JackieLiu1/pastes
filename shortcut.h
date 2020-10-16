#ifndef SHORTCUT_H
#define SHORTCUT_H

#include <QObject>
#include <QTimer>

#include <QThread>

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
};

class DoubleCtrlShortcut : public QObject
{
	Q_OBJECT

public:
	DoubleCtrlShortcut(QObject *parent = nullptr);

signals:
	void activated(void);

private:

	ShortcutPrivate		*m_shortcut;

	QTimer			*m_timer;
	bool			m_isActive;
};

#endif // SHORTCUT_H
