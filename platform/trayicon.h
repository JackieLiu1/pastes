#ifndef PLATFORM_TRAYICON_H
#define PLATFORM_TRAYICON_H

#include <QObject>
#include <memory>

class QIcon;
class QMenu;

/* GUI-thread tray/menu-bar adapter. The context menu remains caller-owned. */
class TrayIcon final : public QObject
{
	Q_OBJECT
public:
	explicit TrayIcon(QObject *parent = nullptr);
	~TrayIcon() override;
	void setIcon(const QIcon &icon);
	void setToolTip(const QString &toolTip);
	void setContextMenu(QMenu *menu);
	void show(void);
	static bool isAvailable(void);

signals:
	void activated(void);

private:
	class NativeState;
	std::unique_ptr<NativeState> m_native;
};

#endif
