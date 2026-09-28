#include <gio/gdesktopappinfo.h>

#include "platform/fileicon.h"

QIcon Platform::fileIcon(const QString &uri)
{
	QString icon_name;
	if (uri.endsWith(".desktop")) {
		auto _desktop_file = g_desktop_app_info_new_from_filename(uri.toUtf8().constData());
		auto _icon_string = g_desktop_app_info_get_string(_desktop_file, "Icon");
		QIcon icon = QIcon::fromTheme(_icon_string, QIcon::fromTheme("text-x-generic"));
		g_free(_icon_string);
		g_object_unref(_desktop_file);
		return icon;
	} else {
		auto file = g_file_new_for_path(uri.toLocal8Bit());
		auto info = g_file_query_info(file,
					      G_FILE_ATTRIBUTE_THUMBNAIL_PATH ","
					      G_FILE_ATTRIBUTE_THUMBNAILING_FAILED ","
					      G_FILE_ATTRIBUTE_STANDARD_ICON,
					      G_FILE_QUERY_INFO_NONE,
					      nullptr,
					      nullptr);
		if (!G_IS_FILE_INFO (info))
			return QIcon();
		GIcon *g_icon = g_file_info_get_icon (info);
		//do not unref the GIcon from info.
		if (G_IS_ICON(g_icon)) {
			const gchar* const* icon_names = g_themed_icon_get_names(G_THEMED_ICON (g_icon));
			if (icon_names)
				icon_name = QString (*icon_names);
		}

		g_object_unref(info);
		g_object_unref(file);
		return QIcon::fromTheme(icon_name, QIcon::fromTheme("text-x-generic"));
	}
}
