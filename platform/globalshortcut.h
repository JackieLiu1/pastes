#ifndef PLATFORM_GLOBALSHORTCUT_H
#define PLATFORM_GLOBALSHORTCUT_H

#include <QObject>

class ShortcutPrivate;

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

#endif // PLATFORM_GLOBALSHORTCUT_H
