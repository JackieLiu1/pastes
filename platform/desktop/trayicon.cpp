#include "platform/trayicon.h"

#include <QSystemTrayIcon>

class TrayIcon::NativeState
{
public:
	QSystemTrayIcon *icon = nullptr;
};

TrayIcon::TrayIcon(QObject *parent) : QObject(parent),
	m_native(std::make_unique<NativeState>())
{
	m_native->icon = new QSystemTrayIcon(this);
	connect(m_native->icon, &QSystemTrayIcon::activated, this,
		[this](QSystemTrayIcon::ActivationReason reason) {
			if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
				emit activated();
		});
}

TrayIcon::~TrayIcon() = default;

void TrayIcon::setIcon(const QIcon &icon) { m_native->icon->setIcon(icon); }
void TrayIcon::setToolTip(const QString &toolTip) { m_native->icon->setToolTip(toolTip); }
void TrayIcon::setContextMenu(QMenu *menu) { m_native->icon->setContextMenu(menu); }
void TrayIcon::show(void) { m_native->icon->show(); }
bool TrayIcon::isAvailable(void) { return QSystemTrayIcon::isSystemTrayAvailable(); }
