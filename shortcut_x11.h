#ifndef SHORTCUT_X11_H
#define SHORTCUT_X11_H

#include <QTimer>
#include <QThread>

class ShortcutPrivateX11 : public QThread
{
	Q_OBJECT

public:
	ShortcutPrivateX11(QObject *parent = nullptr);
	~ShortcutPrivateX11();

Q_SIGNALS:
	void activated(void);

protected:
	void run();

public slots:
	void stop();
};


#endif // SHORTCUT_X11_H
