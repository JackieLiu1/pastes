#include "platform/windowintegration.h"
#include "platform/diagnosticlog.h"

#include <QApplication>
#include <QCursor>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QPointer>
#include <QSocketNotifier>
#include <QTimer>
#include <QWidget>
#include <memory>

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

namespace {
constexpr int nativeFocusIn = FocusIn;
constexpr int nativeFocusOut = FocusOut;
}
#undef FocusIn
#undef FocusOut
#undef KeyPress
#undef KeyRelease

namespace {
class PanelObserver final : public QObject
{
public:
	PanelObserver(QWidget *panel, const std::function<void(bool)> &dismiss)
		: QObject(panel), m_panel(panel), m_dismiss(dismiss),
		  m_log(Platform::DiagnosticLog::inputPath()), m_display(XOpenDisplay(nullptr), &XCloseDisplay)
	{
		if (!m_display) {
			Platform::watchQtPanelDismissal(panel, dismiss);
			return;
		}
		XSelectInput(m_display.get(), panel->winId(), FocusChangeMask | StructureNotifyMask);
		int event = 0, error = 0;
		if (XQueryExtension(m_display.get(), "XInputExtension", &m_inputOpcode, &event, &error)) {
			/* XI 2.1 keeps raw notifications available while Qt's separate
			 * connection grabs the pointer for an owned popup menu. */
			int major = 2, minor = 1;
			m_rawButtons = XIQueryVersion(m_display.get(), &major, &minor) == Success && major >= 2;
		}
		auto *notifier = new QSocketNotifier(ConnectionNumber(m_display.get()), QSocketNotifier::Read, this);
		connect(notifier, &QSocketNotifier::activated, this, [this](void) { drain(); });
		m_focusTimer.setInterval(100);
		connect(&m_focusTimer, &QTimer::timeout, this, [this](void) { drain(); });
		qApp->installEventFilter(this);
		record("session", {{"rawButtons", m_rawButtons}});
	}

	void activate(QWidget *widget)
	{
		if (!m_display || !widget->isVisible()) return;
		m_pending = widget;
		m_activationTime.restart();
		XSelectInput(m_display.get(), widget->winId(), FocusChangeMask | StructureNotifyMask);
		record("activate-request");
		activateMapped();
	}

protected:
	bool eventFilter(QObject *object, QEvent *event) override
	{
		if (object == m_panel) {
			if (event->type() == QEvent::Show) {
				m_seenFocus = false;
				subscribeButtons(true);
				m_focusTimer.start();
				record("show");
			} else if (event->type() == QEvent::Hide) {
				m_pending.clear();
				m_focusTimer.stop();
				subscribeButtons(false);
				record("hide");
			}
		}
		if (event->type() == QEvent::KeyPress && m_panel->isVisible()) {
			auto *widget = qobject_cast<QWidget *>(object);
			if (widget && Platform::isOwnedWindow(m_panel, widget->window())) {
				const auto *key = static_cast<QKeyEvent *>(event);
				const char *name = nullptr;
				switch (key->key()) {
				case Qt::Key_Left: name = "left"; break;
				case Qt::Key_Right: name = "right"; break;
				case Qt::Key_Space: name = "space"; break;
				case Qt::Key_Escape: name = "escape"; break;
				default: break;
				}
				if (name) record("control-key", {{"key", name}, {"repeat", key->isAutoRepeat()},
					{"nativeTimestamp", qint64(key->timestamp())},
					{"receiver", widget->metaObject()->className()}});
			}
		}
		return QObject::eventFilter(object, event);
	}

private:
	void record(const char *event, QJsonObject details = {})
	{
		details.insert("event", event);
		details.insert("visible", m_panel->isVisible());
		details.insert("qtOwnedFocus", Platform::isOwnedWindow(m_panel, QApplication::activeWindow()));
		details.insert("mouseGrab", Platform::isOwnedWindow(m_panel, QWidget::mouseGrabber()));
		details.insert("keyboardGrab", Platform::isOwnedWindow(m_panel, QWidget::keyboardGrabber()));
		m_log.append(details);
	}

	void subscribeButtons(bool enabled)
	{
		if (!m_display || !m_rawButtons) return;
		unsigned char mask[XIMaskLen(XI_RawButtonPress)]{};
		if (enabled) XISetMask(mask, XI_RawButtonPress);
		XIEventMask selection{XIAllMasterDevices, sizeof(mask), mask};
		XISelectEvents(m_display.get(), DefaultRootWindow(m_display.get()), &selection, 1);
		XFlush(m_display.get());
	}

	void activateMapped(void)
	{
		if (!m_pending || !m_pending->isVisible()) { m_pending.clear(); return; }
		XWindowAttributes attributes{};
		if (XGetWindowAttributes(m_display.get(), m_pending->winId(), &attributes) &&
			attributes.map_state == IsViewable) {
			/* The shortcut worker is a different X11 client. Focus must target
			 * the mapped Qt window, including after rapid hide/show reversals. */
			XSetInputFocus(m_display.get(), m_pending->winId(), RevertToPointerRoot, CurrentTime);
			XFlush(m_display.get());
			m_pending.clear();
			record("activate-mapped");
			checkFocus();
		} else if (m_activationTime.elapsed() < 250) {
			QTimer::singleShot(10, this, [this](void) { activateMapped(); });
		} else {
			m_pending.clear();
			record("activate-timeout");
			m_dismiss(false);
		}
	}

	void checkFocus(void)
	{
		if (!m_display || !m_panel->isVisible()) return;
		Window focus = None;
		int revert = 0;
		XGetInputFocus(m_display.get(), &focus, &revert);
		QWidget *focused = QWidget::find(focus);
		const bool owned = Platform::isOwnedWindow(m_panel, focused);
		record("focus", {{"nativeWindow", QString::number(focus)}, {"nativeOwnedFocus", owned}});
		if (owned) {
			m_seenFocus = true;
			/* Qt can finish an old FocusOut after a quick remap. Reconcile
			 * its widget activation only after the server confirms ownership. */
			if (QApplication::activeWindow() != focused->window() &&
				!Platform::isOwnedWindow(m_panel, QApplication::activePopupWidget())) {
				/* activateWindow() cannot generate another FocusIn when this
				 * window already owns native focus. This Qt 6.3 API repairs
				 * widget state; Qt 6.5 deprecated it for ordinary activation. */
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
				QApplication::setActiveWindow(focused->window());
QT_WARNING_POP
				record("restore-qt-activation");
			}
		} else if (m_seenFocus && !m_pending) {
			m_seenFocus = false;
			record("dismiss-focus");
			m_dismiss(false);
		}
	}

	void outsideClick(void)
	{
		if (!m_panel->isVisible()) return;
		QWidget *grabber = QWidget::mouseGrabber();
		if (Platform::isOwnedWindow(m_panel, grabber) && grabber->windowType() != Qt::Popup &&
			grabber->window()->windowType() != Qt::Popup) return;
		QWidget *target = QApplication::widgetAt(QCursor::pos());
		if (Platform::isOwnedWindow(m_panel, target)) return;
		/* Raw button notification observes a click without grabbing or
		 * replaying it. Desktop/taskbar clicks need not change input focus. */
		m_pending.clear();
		record("dismiss-click");
		m_dismiss(false);
	}

	void drain(void)
	{
		while (XPending(m_display.get())) {
			XEvent event{};
			XNextEvent(m_display.get(), &event);
			if (event.type == GenericEvent && event.xcookie.extension == m_inputOpcode &&
				XGetEventData(m_display.get(), &event.xcookie)) {
				if (event.xcookie.evtype == XI_RawButtonPress) {
					const int button = static_cast<XIRawEvent *>(event.xcookie.data)->detail;
					if (button < 4 || button > 7)
						QTimer::singleShot(0, this, [this](void) { outsideClick(); });
				}
				XFreeEventData(m_display.get(), &event.xcookie);
			} else if (event.type == nativeFocusIn || event.type == nativeFocusOut) {
				record(event.type == nativeFocusIn ? "native-focus-in" : "native-focus-out",
					{{"mode", event.xfocus.mode}, {"detail", event.xfocus.detail}});
			}
		}
		checkFocus();
	}

	QWidget *m_panel;
	std::function<void(bool)> m_dismiss;
	Platform::DiagnosticLog m_log;
	std::unique_ptr<Display, decltype(&XCloseDisplay)> m_display;
	QPointer<QWidget> m_pending;
	QTimer m_focusTimer;
	QElapsedTimer m_activationTime;
	int m_inputOpcode = 0;
	bool m_rawButtons = false;
	bool m_seenFocus = false;
};

PanelObserver *observerFor(QWidget *widget)
{
	for (QWidget *owner = widget; owner; owner = owner->parentWidget())
		for (QObject *child : owner->children())
			if (auto *observer = dynamic_cast<PanelObserver *>(child)) return observer;
	return nullptr;
}
}

void Platform::watchPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss)
{
	if (QGuiApplication::platformName() == QStringLiteral("xcb")) new PanelObserver(widget, dismiss);
	else watchQtPanelDismissal(widget, dismiss);
}

void Platform::activatePanel(QWidget *widget)
{
	widget->raise();
	widget->activateWindow();
	if (auto *observer = observerFor(widget)) observer->activate(widget);
}

void Platform::activatePreview(QWidget *widget)
{
	activatePanel(widget);
}
