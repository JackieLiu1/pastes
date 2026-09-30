#include "platform/windowintegration.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <windows.h>
#include <dwmapi.h>

namespace {
enum AccentState { AccentDisabled = 0, AccentBlur = 3, AccentAcrylic = 4 };

struct AccentPolicy
{
	AccentState state;
	DWORD flags;
	DWORD color;
	DWORD animation;
};

struct CompositionAttributeData
{
	int attribute;
	void *data;
	SIZE_T size;
};

using SetCompositionAttribute = BOOL (WINAPI *)(HWND, CompositionAttributeData *);

SetCompositionAttribute compositionFunction(void)
{
	static const auto function = reinterpret_cast<SetCompositionAttribute>(
		GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
	return function;
}

bool systemAllowsBackdrop(void)
{
	HIGHCONTRASTW contrast{sizeof(HIGHCONTRASTW), 0, nullptr};
	if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
		(contrast.dwFlags & HCF_HIGHCONTRASTON)) return false;
	const QSettings personalization(QStringLiteral(
		"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
		QSettings::NativeFormat);
	return personalization.value("EnableTransparency", 1).toInt() != 0;
}

/* The layered Qt window needs the accent API already used by this backend.
 * DWMWA_SYSTEMBACKDROP_TYPE can succeed without compositing behind it.
 * Keep the entry point optional and retain an opaque UI on failure. */
class PanelBackdrop final : public QObject, public QAbstractNativeEventFilter
{
public:
	explicit PanelBackdrop(QWidget *widget) : QObject(widget), m_widget(widget)
	{
		setObjectName(QStringLiteral("PastesWindowsBackdrop"));
		widget->installEventFilter(this);
		qApp->installNativeEventFilter(this);
	}

	~PanelBackdrop(void) override { qApp->removeNativeEventFilter(this); }

	void update(bool refreshSystem = false)
	{
		if (refreshSystem || !m_initialized) m_allowed = systemAllowsBackdrop();
		m_initialized = true;
		const HWND window = reinterpret_cast<HWND>(m_widget->winId());
		const bool dark = qApp->property("pastesDark").toBool();
		const bool enabled = m_allowed &&
			QOperatingSystemVersion::current() >= QOperatingSystemVersion::Windows10;
		const auto function = compositionFunction();
		if (refreshSystem || window != m_window || enabled != m_enabled ||
			dark != m_dark || !m_applied) {
			/* Accent material ignores SetWindowRgn even though the hit-test region
			 * changes. Let DWM clip the material and match that contour in Qt. */
			const DWM_WINDOW_CORNER_PREFERENCE corners = enabled ? DWMWCP_ROUND : DWMWCP_DONOTROUND;
			const bool rounded = SUCCEEDED(DwmSetWindowAttribute(window,
				DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners)));
			AccentPolicy accent{AccentDisabled, 0, 0, 0};
			if (enabled && rounded) {
				accent.state = AccentAcrylic;
				accent.flags = 2;
				accent.color = dark ? 0xA0181818 : 0xA0EFF4F4; // ABGR tint.
			}
			CompositionAttributeData data{19, &accent, sizeof(accent)};
			bool applied = function && function(window, &data);
			if (!applied && accent.state == AccentAcrylic) {
				accent.state = AccentBlur;
				applied = function && function(window, &data);
			}
			m_applied = applied;
			m_enabled = enabled;
			m_dark = dark;
			m_widget->setProperty("pastesPanelBackdrop", enabled && rounded && applied);
			m_widget->update();
		}
		if (m_widget->property("pastesPanelBackdrop").toBool()) {
			/* DWM's standard corner is 8 screen DIPs. Qt can have an additional
			 * application scale factor, so convert through the HWND's real DPI. */
			const qreal radius = GetDpiForWindow(window)/12.0/m_widget->devicePixelRatioF();
			if (m_radius != radius) {
				m_radius = radius;
				m_widget->setProperty("pastesPanelCornerRadius", radius);
				m_widget->setProperty("pastesPanelCorners", 0);
				m_widget->update();
			}
		}
		m_window = window;
	}

	bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
	{
		const auto *event = static_cast<MSG *>(message);
		if (event->hwnd == m_window && (event->message == WM_SETTINGCHANGE ||
			event->message == WM_THEMECHANGED || event->message == WM_DWMCOMPOSITIONCHANGED ||
			event->message == WM_DPICHANGED))
			QTimer::singleShot(0, this, [this] { update(true); });
		return false;
	}

	bool eventFilter(QObject *, QEvent *event) override
	{
		if (event->type() == QEvent::WinIdChange)
			QTimer::singleShot(0, this, [this] { update(); });
		return false;
	}

private:
	QWidget *m_widget;
	HWND m_window = nullptr;
	qreal m_radius = 0;
	bool m_initialized = false;
	bool m_allowed = false;
	bool m_enabled = false;
	bool m_dark = false;
	bool m_applied = false;
};

void updateBackdrop(QWidget *widget, bool refreshSystem)
{
	if (QGuiApplication::platformName() != QStringLiteral("windows")) {
		widget->setProperty("pastesPanelBackdrop", false);
		return;
	}
	auto *backdrop = static_cast<PanelBackdrop *>(
		widget->findChild<QObject *>(QStringLiteral("PastesWindowsBackdrop"), Qt::FindDirectChildrenOnly));
	if (!backdrop) backdrop = new PanelBackdrop(widget);
	backdrop->update(refreshSystem);
}
}

const Platform::PanelAppearance &Platform::panelAppearance(void)
{
	static const PanelAppearance appearance = [] {
		PanelAppearance value;
		value.nativeBackdrop = true;
		value.shadow = false;
		return value;
	}();
	return appearance;
}

void Platform::enablePanelBlur(QWidget *widget) { updateBackdrop(widget, true); }
void Platform::preparePanel(QWidget *widget) { updateBackdrop(widget, true); }
void Platform::updatePanelBackdrop(QWidget *widget) { updateBackdrop(widget, false); }
