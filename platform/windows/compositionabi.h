#ifndef PASTES_WINDOWS_COMPOSITIONABI_H
#define PASTES_WINDOWS_COMPOSITIONABI_H

#include <windows.ui.composition.h>
#include <windows.graphics.h>
#include <d2d1.h>

/* MinGW supplies the base WinRT interfaces but only forward-declares these
 * newer interfaces. Declare the leading ABI methods we use, in SDK order.
 * Objects are created by Windows; these declarations are never implemented.
 * See Microsoft's windows.ui.composition{,.interop}.h and
 * windows.graphics.interop.h in the Windows SDK. */
namespace WindowsComposition {
struct DesktopInterop : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE CreateDesktopWindowTarget(HWND, BOOL, IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE EnsureOnThread(DWORD) = 0;
};
struct Compositor3 : IInspectable
{
	virtual HRESULT STDMETHODCALLTYPE CreateHostBackdropBrush(IInspectable **) = 0;
};
struct Compositor5 : IInspectable
{
	virtual HRESULT STDMETHODCALLTYPE get_Comment(HSTRING *) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_Comment(HSTRING) = 0;
	virtual HRESULT STDMETHODCALLTYPE get_GlobalPlaybackRate(float *) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_GlobalPlaybackRate(float) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateBounceScalarAnimation(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateBounceVector2Animation(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateBounceVector3Animation(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateContainerShape(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateEllipseGeometry(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateLineGeometry(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePathGeometry(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreatePathGeometryWithPath(IInspectable *, IInspectable **) = 0;
};
struct Compositor6 : IInspectable
{
	virtual HRESULT STDMETHODCALLTYPE CreateGeometricClip(IInspectable **) = 0;
	virtual HRESULT STDMETHODCALLTYPE CreateGeometricClipWithGeometry(IInspectable *, IInspectable **) = 0;
};
struct PathFactory : IInspectable
{
	virtual HRESULT STDMETHODCALLTYPE Create(ABI::Windows::Graphics::IGeometrySource2D *, IInspectable **) = 0;
};
struct GeometryInterop : IUnknown
{
	virtual HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry **) = 0;
	virtual HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(ID2D1Factory *, ID2D1Geometry **) = 0;
};
inline constexpr IID DesktopId{0x29e691fa, 0x4567, 0x4dca,
	{0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07}};
inline constexpr IID Compositor3Id{0xc9dd8ef0, 0x6eb1, 0x4e3c,
	{0xa6, 0x58, 0x67, 0x5d, 0x9c, 0x64, 0xd4, 0xab}};
inline constexpr IID Compositor5Id{0x48ea31ad, 0x7fcd, 0x4076,
	{0xa7, 0x9c, 0x90, 0xcc, 0x4b, 0x85, 0x2c, 0x9b}};
inline constexpr IID Compositor6Id{0x7a38b2bd, 0xcec8, 0x4eeb,
	{0x83, 0x0f, 0xd8, 0xd0, 0x7a, 0xed, 0xeb, 0xc3}};
inline constexpr IID PathFactoryId{0x9c1e8c6a, 0x0f33, 0x4751,
	{0x94, 0x37, 0xeb, 0x3f, 0xb9, 0xd3, 0xab, 0x07}};
inline constexpr IID GeometryId{0xe985217c, 0x6a17, 0x4207,
	{0xab, 0xd8, 0x5f, 0xd3, 0xdd, 0x61, 0x2a, 0x9d}};
inline constexpr IID GeometryInteropId{0x0657af73, 0x53fd, 0x47cf,
	{0x84, 0xff, 0xc8, 0x49, 0x2d, 0x2a, 0x80, 0xa3}};
}
#endif
