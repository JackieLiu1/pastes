#include "platform/windows/compositionabi.h"
#include <dispatcherqueue.h>
#include <dwmapi.h>
#include <roapi.h>
#include <winstring.h>
#include <wrl/client.h>

#include "platform/windowintegration.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QEvent>
#include <QSettings>
#include <QTimer>
#include <QWidget>
#include <atomic>

namespace {
using Microsoft::WRL::ComPtr;
namespace Composition = ABI::Windows::UI::Composition;
namespace Native = WindowsComposition;

class Runtime final : public QObject
{
public:
	Runtime(void) : QObject(qApp)
	{
		setObjectName(QStringLiteral("PastesCompositionRuntime"));
		m_initialized = SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED));
		if (!m_initialized) return;
		static const HMODULE module = LoadLibraryExW(L"CoreMessaging.dll", nullptr,
			LOAD_LIBRARY_SEARCH_SYSTEM32);
		using CreateQueue = HRESULT (WINAPI *)(DispatcherQueueOptions,
			ABI::Windows::System::IDispatcherQueueController **);
		const auto create = module ? reinterpret_cast<CreateQueue>(
			GetProcAddress(module, "CreateDispatcherQueueController")) : nullptr;
		DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
		m_ready = create && SUCCEEDED(create(options, &m_queue));
	}
	~Runtime(void) override
	{
		if (m_queue) {
			ComPtr<ABI::Windows::Foundation::IAsyncAction> shutdown;
			m_queue->ShutdownQueueAsync(&shutdown);
			m_queue.Reset();
		}
		if (m_initialized) RoUninitialize();
	}
	bool ready(void) const { return m_ready; }
private:
	ComPtr<ABI::Windows::System::IDispatcherQueueController> m_queue;
	bool m_initialized = false;
	bool m_ready = false;
};

bool runtimeReady(void)
{
	auto *runtime = static_cast<Runtime *>(qApp->findChild<QObject *>(
		QStringLiteral("PastesCompositionRuntime"), Qt::FindDirectChildrenOnly));
	if (!runtime) runtime = new Runtime;
	return runtime->ready();
}

class GeometrySource final : public ABI::Windows::Graphics::IGeometrySource2D,
	public Native::GeometryInterop
{
public:
	explicit GeometrySource(ID2D1Geometry *geometry) : m_geometry(geometry) {}
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override
	{
		if (!out) return E_POINTER;
		*out = nullptr;
		if (id == __uuidof(IUnknown) || id == __uuidof(IInspectable) ||
			id == __uuidof(ABI::Windows::Graphics::IGeometrySource2D))
			*out = static_cast<ABI::Windows::Graphics::IGeometrySource2D *>(this);
		else if (id == Native::GeometryInteropId)
			*out = static_cast<Native::GeometryInterop *>(this);
		else return E_NOINTERFACE;
		AddRef(); return S_OK;
	}
	ULONG STDMETHODCALLTYPE AddRef(void) override { return ++m_references; }
	ULONG STDMETHODCALLTYPE Release(void) override
	{
		const ULONG remaining = --m_references;
		if (!remaining) delete this;
		return remaining;
	}
	HRESULT STDMETHODCALLTYPE GetIids(ULONG *count, IID **ids) override
	{ *count = 0; *ids = nullptr; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING *name) override
	{ *name = nullptr; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel *trust) override
	{ *trust = BaseTrust; return S_OK; }
	HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry **geometry) override
	{ return m_geometry.CopyTo(geometry); }
	HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(ID2D1Factory *, ID2D1Geometry **geometry) override
	{ *geometry = nullptr; return S_OK; }
private:
	std::atomic<ULONG> m_references{1};
	ComPtr<ID2D1Geometry> m_geometry;
};

template<typename T>
HRESULT query(IUnknown *object, REFIID id, ComPtr<T> &result)
{ return object->QueryInterface(id, reinterpret_cast<void **>(result.GetAddressOf())); }

HRESULT activation(const wchar_t *name, REFIID id, void **result)
{
	HSTRING string = nullptr;
	HRESULT status = WindowsCreateString(name, UINT32(wcslen(name)), &string);
	if (SUCCEEDED(status)) status = RoGetActivationFactory(string, id, result);
	WindowsDeleteString(string);
	return status;
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

LRESULT CALLBACK backdropProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_NCHITTEST) return HTTRANSPARENT;
	if (message == WM_ERASEBKGND) return 1;
	if (message == WM_PAINT) {
		PAINTSTRUCT paint;
		BeginPaint(window, &paint); EndPaint(window, &paint); return 0;
	}
	return DefWindowProcW(window, message, wparam, lparam);
}

bool enableHostBackdrop(HWND window)
{
	const BOOL enabled = TRUE;
	if (SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_USE_HOSTBACKDROPBRUSH,
		&enabled, sizeof(enabled)))) return true;
	/* Windows 10 exposes host sampling through the same optional entry point
	 * used by the old backend. State 5 enables sampling without drawing a
	 * rectangular acrylic plate. Failure keeps the Qt surface opaque. */
	struct Accent { int state; DWORD flags, color, animation; } accent{5, 0, 0, 0};
	struct Attribute { int id; void *data; SIZE_T size; } attribute{19, &accent, sizeof(accent)};
	using SetAttribute = BOOL (WINAPI *)(HWND, Attribute *);
	const auto function = reinterpret_cast<SetAttribute>(GetProcAddress(
		GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
	return function && function(window, &attribute);
}

/* Qt's layered backing store and DWM's accent material have separate clips.
 * Put a host-backdrop visual in a nonactivating owner behind the Qt window,
 * and clip that visual itself. Cards and input stay in the original HWND. */
class PanelBackdrop final : public QObject, public QAbstractNativeEventFilter
{
public:
	explicit PanelBackdrop(QWidget *widget) : QObject(widget), m_widget(widget)
	{
		setObjectName(QStringLiteral("PastesWindowsBackdrop"));
		widget->installEventFilter(this);
		qApp->installNativeEventFilter(this);
	}
	~PanelBackdrop(void) override
	{
		qApp->removeNativeEventFilter(this);
		clear();
	}
	void update(bool refreshSystem = false)
	{
		if (m_updating) return;
		m_updating = true;
		const HWND window = reinterpret_cast<HWND>(m_widget->winId());
		if (window != m_window) {
			clear(); m_window = window; m_failed = false;
			const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
			DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
		}
		if (refreshSystem || !m_initialized) {
			m_allowed = systemAllowsBackdrop(); m_initialized = true; m_failed = false;
		}
		if (!m_allowed) clear();
		if (m_allowed && !m_helper && !m_failed) {
			const HRESULT status = create();
			if (FAILED(status)) { clear(); m_failed = true; }
		}
		if (m_helper && FAILED(synchronize())) { clear(); m_failed = true; }
		const bool active = m_helper != nullptr;
		if (m_widget->property("pastesPanelBackdrop").toBool() != active) {
			m_widget->setProperty("pastesPanelBackdrop", active); m_widget->update();
		}
		m_updating = false;
	}
	bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
	{
		const auto *event = static_cast<MSG *>(message);
		if (event->hwnd != m_window) return false;
		if (event->message == WM_WINDOWPOSCHANGED || event->message == WM_SHOWWINDOW)
			update();
		else if (event->message == WM_SETTINGCHANGE || event->message == WM_THEMECHANGED ||
			event->message == WM_DWMCOMPOSITIONCHANGED || event->message == WM_DPICHANGED)
			QTimer::singleShot(0, this, [this] { update(true); });
		else if (event->message == WM_NCDESTROY) {
			m_updating = true; clear(); m_updating = false;
		}
		return false;
	}
	bool eventFilter(QObject *, QEvent *event) override
	{
		if (event->type() == QEvent::Hide && m_helper) ShowWindow(m_helper, SW_HIDE);
		if (event->type() == QEvent::WinIdChange || event->type() == QEvent::Show)
			QTimer::singleShot(0, this, [this] { update(); });
		return false;
	}
private:
	HRESULT create(void)
	{
		if (!runtimeReady()) return E_FAIL;
		HSTRING name = nullptr;
		HRESULT status = WindowsCreateString(L"Windows.UI.Composition.Compositor", 33, &name);
		ComPtr<IInspectable> instance;
		if (SUCCEEDED(status)) status = RoActivateInstance(name, &instance);
		WindowsDeleteString(name);
		if (FAILED(status)) return status;
		if (FAILED(status = instance.As(&m_compositor))) return status;
		if (FAILED(status = query(instance.Get(), Native::Compositor5Id, m_paths))) return status;
		if (FAILED(status = query(instance.Get(), Native::Compositor6Id, m_clips))) return status;
		ComPtr<Native::Compositor3> brushes;
		if (FAILED(status = query(instance.Get(), Native::Compositor3Id, brushes))) return status;
		ComPtr<IInspectable> host;
		if (FAILED(status = brushes->CreateHostBackdropBrush(&host))) return status;
		ComPtr<Composition::ICompositionBrush> brush;
		if (FAILED(status = host.As(&brush))) return status;
		ComPtr<Composition::ISpriteVisual> sprite;
		if (FAILED(status = m_compositor->CreateSpriteVisual(&sprite))) return status;
		if (FAILED(status = sprite->put_Brush(brush.Get()))) return status;
		if (FAILED(status = sprite.As(&m_visual))) return status;
		if (FAILED(status = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
			__uuidof(ID2D1Factory), nullptr, reinterpret_cast<void **>(m_factory.GetAddressOf()))))
			return status;
		if (FAILED(status = activation(L"Windows.UI.Composition.CompositionPath",
			Native::PathFactoryId, reinterpret_cast<void **>(m_pathFactory.GetAddressOf())))) return status;
		static const wchar_t className[] = L"PastesCompositionBackdrop";
		static const ATOM windowClass = [] {
			WNDCLASSW value{};
			value.lpfnWndProc = backdropProcedure;
			value.hInstance = GetModuleHandleW(nullptr);
			value.lpszClassName = className;
			return RegisterClassW(&value);
		}();
		if (!windowClass) return E_FAIL;
		m_owner = reinterpret_cast<HWND>(GetWindowLongPtrW(m_window, GWLP_HWNDPARENT));
		m_helper = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW |
			WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOPMOST, className, L"", WS_POPUP,
			0, 0, 1, 1, m_owner, nullptr, GetModuleHandleW(nullptr), nullptr);
		if (!m_helper) return E_FAIL;
		const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DONOTROUND;
		DwmSetWindowAttribute(m_helper, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
		if (!enableHostBackdrop(m_helper)) return E_FAIL;
		ComPtr<Native::DesktopInterop> desktop;
		if (FAILED(status = query(instance.Get(), Native::DesktopId, desktop))) return status;
		ComPtr<IInspectable> target;
		if (FAILED(status = desktop->CreateDesktopWindowTarget(m_helper, FALSE, &target))) return status;
		if (FAILED(status = target.As(&m_target))) return status;
		if (FAILED(status = m_target->put_Root(m_visual.Get()))) return status;
		SetLastError(0);
		SetWindowLongPtrW(m_window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_helper));
		return GetLastError() ? E_FAIL : S_OK;
	}
	HRESULT synchronize(void)
	{
		if (reinterpret_cast<HWND>(GetWindowLongPtrW(m_window, GWLP_HWNDPARENT)) != m_helper) {
			SetLastError(0);
			SetWindowLongPtrW(m_window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_helper));
			if (GetLastError()) return E_FAIL;
		}
		RECT rect{};
		if (!GetWindowRect(m_window, &rect)) return E_FAIL;
		const QSize size(rect.right-rect.left, rect.bottom-rect.top);
		const qreal ratio = m_widget->devicePixelRatioF();
		if (size.isEmpty()) return S_OK;
		if (size != m_size || ratio != m_ratio) {
			const float width = size.width(), height = size.height();
			const float radius = qMin(qreal(qMin(width, height)/2),
				Platform::panelAppearance().cornerRadius*ratio);
			const float arc = radius*0.5522847498f;
			ComPtr<ID2D1PathGeometry> path;
			HRESULT status = m_factory->CreatePathGeometry(&path);
			if (FAILED(status)) return status;
			ComPtr<ID2D1GeometrySink> sink;
			if (FAILED(status = path->Open(&sink))) return status;
			sink->BeginFigure(D2D1::Point2F(0, height), D2D1_FIGURE_BEGIN_FILLED);
			sink->AddLine(D2D1::Point2F(0, radius));
			sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(0, radius-arc),
				D2D1::Point2F(radius-arc, 0), D2D1::Point2F(radius, 0)));
			sink->AddLine(D2D1::Point2F(width-radius, 0));
			sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(width-radius+arc, 0),
				D2D1::Point2F(width, radius-arc), D2D1::Point2F(width, radius)));
			sink->AddLine(D2D1::Point2F(width, height));
			sink->EndFigure(D2D1_FIGURE_END_CLOSED);
			if (FAILED(status = sink->Close())) return status;
			ComPtr<ABI::Windows::Graphics::IGeometrySource2D> source;
			source.Attach(new GeometrySource(path.Get()));
			ComPtr<IInspectable> compositionPath, rawGeometry, geometry, rawClip;
			if (FAILED(status = m_pathFactory->Create(source.Get(), &compositionPath))) return status;
			if (FAILED(status = m_paths->CreatePathGeometryWithPath(compositionPath.Get(), &rawGeometry))) return status;
			if (FAILED(status = query(rawGeometry.Get(), Native::GeometryId, geometry))) return status;
			if (FAILED(status = m_clips->CreateGeometricClipWithGeometry(geometry.Get(), &rawClip))) return status;
			ComPtr<Composition::ICompositionClip> clip;
			if (FAILED(status = rawClip.As(&clip))) return status;
			if (FAILED(status = m_visual->put_Clip(clip.Get()))) return status;
			if (FAILED(status = m_visual->put_Size({width, height}))) return status;
			m_size = size; m_ratio = ratio;
		}
		const bool visible = IsWindowVisible(m_window) && !IsIconic(m_window) &&
			!m_widget->testAttribute(Qt::WA_DontShowOnScreen);
		/* Ownership keeps Qt above the material, without stealing activation. */
		if (!SetWindowPos(m_helper, m_window, rect.left, rect.top, size.width(), size.height(),
			SWP_NOACTIVATE | (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW))) return E_FAIL;
		return S_OK;
	}
	void clear(void)
	{
		if (m_helper) {
			ShowWindow(m_helper, SW_HIDE);
			/* Destroying an owner also destroys its owned windows. Restore Qt's
			 * previous owner before releasing our helper. */
			if (IsWindow(m_window) && reinterpret_cast<HWND>(
				GetWindowLongPtrW(m_window, GWLP_HWNDPARENT)) == m_helper)
				SetWindowLongPtrW(m_window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(m_owner));
		}
		if (m_target) m_target->put_Root(nullptr);
		m_target.Reset(); m_visual.Reset(); m_paths.Reset(); m_clips.Reset();
		m_compositor.Reset(); m_pathFactory.Reset(); m_factory.Reset();
		if (m_helper) DestroyWindow(m_helper);
		m_helper = nullptr; m_owner = nullptr; m_size = QSize(); m_ratio = 0;
	}
	QWidget *m_widget;
	HWND m_window = nullptr, m_helper = nullptr, m_owner = nullptr;
	ComPtr<Composition::ICompositor> m_compositor;
	ComPtr<Composition::ICompositionTarget> m_target;
	ComPtr<Composition::IVisual> m_visual;
	ComPtr<Native::Compositor5> m_paths;
	ComPtr<Native::Compositor6> m_clips;
	ComPtr<Native::PathFactory> m_pathFactory;
	ComPtr<ID2D1Factory> m_factory;
	QSize m_size;
	qreal m_ratio = 0;
	bool m_initialized = false, m_allowed = false, m_failed = false, m_updating = false;
};

void updateBackdrop(QWidget *widget, bool refreshSystem)
{
	if (QGuiApplication::platformName() != QStringLiteral("windows")) {
		widget->setProperty("pastesPanelBackdrop", false); return;
	}
	auto *backdrop = static_cast<PanelBackdrop *>(widget->findChild<QObject *>(
		QStringLiteral("PastesWindowsBackdrop"), Qt::FindDirectChildrenOnly));
	if (!backdrop) backdrop = new PanelBackdrop(widget);
	backdrop->update(refreshSystem);
}
}

const Platform::PanelAppearance &Platform::panelAppearance(void)
{
	static const PanelAppearance appearance = [] {
		PanelAppearance value;
		value.nativeBackdrop = true; value.shadow = false;
		return value;
	}();
	return appearance;
}

void Platform::enablePanelBlur(QWidget *widget) { updateBackdrop(widget, true); }
void Platform::preparePanel(QWidget *widget) { updateBackdrop(widget, true); }
void Platform::updatePanelBackdrop(QWidget *widget) { updateBackdrop(widget, false); }
