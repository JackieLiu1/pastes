#include "platform/windowintegration.h"

#include <QWidget>
#include <QApplication>
#include <QTimer>

namespace {
class QtPanelObserver final : public QObject
{
public:
	QtPanelObserver(QWidget *widget, const std::function<void(bool)> &dismiss)
		: QObject(widget), m_widget(widget), m_dismiss(dismiss)
	{
		widget->installEventFilter(this);
	}
protected:
	bool eventFilter(QObject *, QEvent *event) override
	{
		if (event->type() == QEvent::ActivationChange)
			QTimer::singleShot(0, this, [this](void) {
				if (m_widget->isVisible() && !Platform::isOwnedWindow(m_widget, QApplication::activeWindow()))
					m_dismiss(false);
			});
		return false;
	}
private:
	QWidget *m_widget;
	std::function<void(bool)> m_dismiss;
};
}

void Platform::watchQtPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss)
{
	new QtPanelObserver(widget, dismiss);
}

bool Platform::isOwnedWindow(QWidget *owner, QWidget *window)
{
	for (QWidget *parent = window; parent; parent = parent->parentWidget())
		if (parent == owner)
			return true;
	return false;
}

void Platform::initializeDragOverlay(QWidget *widget)
{
	if (QWidget *owner = widget->parentWidget()) {
		/* A managed tool window can appear in the taskbar and sit below an
		 * unmanaged X11 panel. Inherit its stacking policy before mapping. */
		widget->setWindowFlags(widget->windowFlags() | (owner->window()->windowFlags() &
			(Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint)));
	}
}
