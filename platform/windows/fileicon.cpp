#include "platform/fileicon.h"

#include <QDir>
#include <QPixmap>
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>

namespace {
/* Qt6's native conversion also handles legacy masks and icons without alpha. */
QPixmap pixmapFromHICON(HICON icon)
{
	return icon ? QPixmap::fromImage(QImage::fromHICON(icon)) : QPixmap();
}

QPixmap pixmapFromShellImageList(int iImageList, const SHFILEINFO &info)
{
	QPixmap result;
	// For MinGW:
	static const IID iID_IImageList = {0x46eb5926, 0x582e, 0x4017, {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x9, 0x50}};

	IImageList *imageList = nullptr;
	if (FAILED(SHGetImageList(iImageList, iID_IImageList, reinterpret_cast<void **>(&imageList))))
		return result;

	HICON hIcon = 0;
	if (SUCCEEDED(imageList->GetIcon(info.iIcon, ILD_TRANSPARENT, &hIcon))) {
		result = pixmapFromHICON(hIcon);
		DestroyIcon(hIcon);
	}
	imageList->Release();

	return result;
}
}

QIcon Platform::fileIcon(const QString &uri)
{
	if (!uri.isEmpty()) {
		const QString nativeName = QDir::toNativeSeparators(uri);
		const wchar_t *sourceFileC = reinterpret_cast<const wchar_t *>(nativeName.utf16());

		SHFILEINFO  info;
		if(SHGetFileInfo(sourceFileC,
				 0,
				 &info,
				 sizeof(info),
				 SHGFI_SYSICONINDEX| SHGFI_ICON |  SHGFI_LARGEICON))
		{
			QIcon icon;

			const QPixmap extraLarge = pixmapFromShellImageList(0x4, info);
			icon.addPixmap(extraLarge);
			if (info.hIcon)
				DestroyIcon(info.hIcon);

			return icon;
		}
	}

	return QIcon();
}
