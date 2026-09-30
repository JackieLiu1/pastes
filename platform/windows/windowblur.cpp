#include "platform/windowintegration.h"

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QOperatingSystemVersion>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <QtMath>
#include <windows.h>

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
		const auto version = QOperatingSystemVersion::current();
		const bool enabled = m_allowed && version >= QOperatingSystemVersion::Windows10;
		const auto function = compositionFunction();
		if (refreshSystem || window != m_window || enabled != m_enabled ||
			dark != m_dark || !m_applied) {
			AccentPolicy accent{AccentDisabled, 0, 0, 0};
			if (enabled) {
				/* Acrylic is available starting with Windows 10 April 2018. */
				accent.state = version.microVersion() >= 17134 ? AccentAcrylic : AccentBlur;
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
			m_widget->setProperty("pastesPanelBackdrop", enabled && applied);
			m_widget->update();
		}
		RECT rect{};
		if (GetClientRect(window, &rect)) {
			const QSize pixels(rect.right, rect.bottom);
			const int radius = qRound(18*m_widget->devicePixelRatioF());
			if (window != m_window || pixels != m_pixels || radius != m_radius) {
				/* Clip the native material as well as Qt's top-only round corners. */
				HRGN region = CreateRoundRectRgn(0, 0, pixels.width()+1,
					pixels.height()+1, radius*2, radius*2);
				HRGN bottom = CreateRectRgn(0, radius, pixels.width(), pixels.height());
				if (region && bottom) {
					CombineRgn(region, region, bottom, RGN_OR);
					if (SetWindowRgn(window, region, TRUE)) region = nullptr;
				}
				if (region) DeleteObject(region);
				if (bottom) DeleteObject(bottom);
				m_pixels = pixels;
				m_radius = radius;
			}
		}
		m_window = window;
	}

	bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
	{
		const auto *event = static_cast<MSG *>(message);
		if (event->hwnd == m_window && (event->message == WM_SETTINGCHANGE ||
			event->message == WM_THEMECHANGED || event->message == WM_DWMCOMPOSITIONCHANGED))
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
	QSize m_pixels;
	int m_radius = 0;
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
