#include "platform/linux/clipboardicon.h"
#include "core/sourceicon.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QStandardPaths>
#include <QSysInfo>
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <xcb/xcb.h>
#include <xcb/res.h>
/* GIO's signals field conflicts with Qt's keyword macro. */
#undef signals
#include <gio/gdesktopappinfo.h>

namespace {

template<typename T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
template<typename T> Reply<T> reply(T *value) { return Reply<T>(value, &std::free); }

constexpr uint32_t maxPropertyWords = 4*1024*1024;
constexpr int maxWindows = 256;

class Connection final
{
public:
	Connection(void) : m_connection(xcb_connect(nullptr, &m_screen), &xcb_disconnect)
	{
		if (xcb_connection_has_error(m_connection.get())) return;
		auto screens = xcb_setup_roots_iterator(xcb_get_setup(m_connection.get()));
		for (int i = 0; i < m_screen && screens.rem; ++i) xcb_screen_next(&screens);
		if (screens.rem) root = screens.data->root;
		const auto *extension = xcb_get_extension_data(get(), &xcb_res_id);
		if (!extension || !extension->present) return;
		m_hasResources = true;
		auto version = reply(xcb_res_query_version_reply(get(),
			xcb_res_query_version(get(), 1, 2), nullptr));
		m_hasProcessIds = version && (version->server_major > 1 ||
			(version->server_major == 1 && version->server_minor >= 2));
	}

	xcb_connection_t *get(void) const { return m_connection.get(); }
	xcb_window_t root = XCB_NONE;

	xcb_atom_t atom(const char *name)
	{
		const auto cached = m_atoms.constFind(name);
		if (cached != m_atoms.constEnd()) return *cached;
		auto value = reply(xcb_intern_atom_reply(get(),
			xcb_intern_atom(get(), false, uint16_t(qstrlen(name)), name), nullptr));
		const auto result = value ? value->atom : xcb_atom_t(XCB_NONE);
		m_atoms.insert(name, result);
		return result;
	}

	Reply<xcb_get_property_reply_t> property(xcb_window_t window, xcb_atom_t name,
		xcb_atom_t type, uint32_t limit = 1024)
	{
		/* Checked replies discard BadWindow locally if an application exits. */
		return reply(xcb_get_property_reply(get(),
			xcb_get_property(get(), false, window, name, type, 0, limit), nullptr));
	}

	quint32 number(xcb_window_t window, xcb_atom_t name, xcb_atom_t type)
	{
		auto value = property(window, name, type, 1);
		return value && value->type == type && value->format == 32 && value->value_len == 1 ?
			*static_cast<const quint32 *>(xcb_get_property_value(value.get())) : 0;
	}

	QString text(xcb_window_t window, xcb_atom_t name)
	{
		auto value = property(window, name, XCB_GET_PROPERTY_TYPE_ANY);
		if (!value || value->format != 8 || value->bytes_after) return {};
		return QString::fromUtf8(static_cast<const char *>(xcb_get_property_value(value.get())),
			xcb_get_property_value_length(value.get())).remove(QChar('\0'));
	}

	quint32 processId(xcb_window_t window)
	{
		if (!m_hasProcessIds) return 0;
		const xcb_res_client_id_spec_t spec{window, XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID};
		auto ids = reply(xcb_res_query_client_ids_reply(get(),
			xcb_res_query_client_ids(get(), 1, &spec), nullptr));
		if (!ids) return 0;
		for (auto i = xcb_res_query_client_ids_ids_iterator(ids.get()); i.rem;
			xcb_res_client_id_value_next(&i)) {
			if ((i.data->spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) &&
				xcb_res_client_id_value_value_length(i.data) == 1)
				return *xcb_res_client_id_value_value(i.data);
		}
		return 0;
	}

	xcb_res_client_t client(xcb_window_t window)
	{
		if (!m_hasResources) return {};
		auto clients = reply(xcb_res_query_clients_reply(get(), xcb_res_query_clients(get()), nullptr));
		if (!clients) return {};
		for (auto i = xcb_res_query_clients_clients_iterator(clients.get()); i.rem;
			xcb_res_client_next(&i))
			if ((window & ~i.data->resource_mask) == i.data->resource_base) return *i.data;
		return {};
	}

	QList<xcb_window_t> windows(xcb_atom_t clientList)
	{
		QList<xcb_window_t> result;
		auto list = property(root, clientList, XCB_ATOM_WINDOW, maxWindows);
		if (list && list->type == XCB_ATOM_WINDOW && list->format == 32) {
			const auto *data = static_cast<const xcb_window_t *>(xcb_get_property_value(list.get()));
			for (uint32_t i = 0; i < list->value_len; ++i) result.append(data[i]);
		}
		if (!result.isEmpty()) return result;
		/* Bare X servers have no EWMH list. Include one level below WM frames. */
		auto appendChildren = [&](xcb_window_t parent) {
			auto tree = reply(xcb_query_tree_reply(get(), xcb_query_tree(get(), parent), nullptr));
			if (!tree) return;
			const auto *children = xcb_query_tree_children(tree.get());
			for (int i = 0; i < tree->children_len && result.size() < maxWindows; ++i)
				result.append(children[i]);
		};
		appendChildren(root);
		const auto topLevel = result;
		for (xcb_window_t window : topLevel) appendChildren(window);
		return result;
	}

private:
	int m_screen = 0;
	std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> m_connection;
	bool m_hasResources = false;
	bool m_hasProcessIds = false;
	QHash<QByteArray, xcb_atom_t> m_atoms;
};

void addIdentity(Connection &connection, xcb_window_t window, LinuxClipboardIcon::Source &source)
{
	auto classes = connection.property(window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING);
	if (classes && classes->type == XCB_ATOM_STRING && classes->format == 8 && !classes->bytes_after) {
		const QByteArray names(static_cast<const char *>(xcb_get_property_value(classes.get())),
			xcb_get_property_value_length(classes.get()));
		for (const QByteArray &name : names.split('\0'))
			if (!name.isEmpty()) source.classes.append(QString::fromUtf8(name));
	}
	for (const char *name : {"_GTK_APPLICATION_ID", "_KDE_NET_WM_DESKTOP_FILE"}) {
		const QString id = connection.text(window, connection.atom(name));
		if (!id.isEmpty()) source.desktopIds.append(QFileInfo(id).fileName());
	}
	source.classes.removeDuplicates();
	source.desktopIds.removeDuplicates();
}

QString desktopId(QString value)
{
	if (value.endsWith(".desktop")) value.chop(8);
	return value.toCaseFolded();
}

}

QImage LinuxClipboardIcon::decodeIcon(const quint32 *data, qsizetype count)
{
	if (!data || count <= 0 || count > maxPropertyWords) return {};
	qsizetype best = -1;
	quint64 bestArea = 0;
	bool bestLarge = false;
	for (qsizetype offset = 0; offset < count;) {
		if (count-offset < 2) return {};
		const quint32 width = data[offset], height = data[offset+1];
		const quint64 area = quint64(width)*height;
		if (!width || !height || width > 4096 || height > 4096 || area > quint64(count-offset-2))
			return {};
		const bool large = qMin(width, height) >= quint32(SourceIcon::maxPixels);
		if (best < 0 || (large && !bestLarge) || (large == bestLarge &&
			(large ? area < bestArea : area > bestArea))) {
			best = offset;
			bestArea = area;
			bestLarge = large;
		}
		offset += qsizetype(area)+2;
	}
	QImage image(int(data[best]), int(data[best+1]), QImage::Format_ARGB32);
	if (image.isNull()) return {};
	for (int y = 0; y < image.height(); ++y)
		std::copy_n(data+best+2+qsizetype(y)*image.width(), image.width(),
			reinterpret_cast<quint32 *>(image.scanLine(y)));
	return SourceIcon::bounded(image);
}

LinuxClipboardIcon::Source LinuxClipboardIcon::captureSource(void)
{
	Source source;
	/* An Xwayland owner cannot identify a native Wayland clipboard source. */
	if (QGuiApplication::platformName() != "xcb") return source;
	Connection connection;
	if (!connection.root) return source;
	const auto clipboard = connection.atom("CLIPBOARD");
	auto owner = reply(xcb_get_selection_owner_reply(connection.get(),
		xcb_get_selection_owner(connection.get(), clipboard), nullptr));
	if (!owner || !owner->owner) return source;
	source.owner = owner->owner;
	const auto iconAtom = connection.atom("_NET_WM_ICON");
	const auto pidAtom = connection.atom("_NET_WM_PID");
	const auto leaderAtom = connection.atom("WM_CLIENT_LEADER");
	const auto machineAtom = connection.atom("WM_CLIENT_MACHINE");
	auto windowIcon = [&](xcb_window_t window) {
		auto value = connection.property(window, iconAtom, XCB_ATOM_CARDINAL, maxPropertyWords);
		return value && value->type == XCB_ATOM_CARDINAL && value->format == 32 && !value->bytes_after ?
			decodeIcon(static_cast<const quint32 *>(xcb_get_property_value(value.get())), value->value_len) : QImage();
	};
	const quint32 nativePid = connection.processId(source.owner);
	source.processId = nativePid ? nativePid : connection.number(source.owner, pidAtom, XCB_ATOM_CARDINAL);
	const QString machine = connection.text(source.owner, machineAtom);
	if (source.processId && (nativePid || machine == QSysInfo::machineHostName()))
		source.executable = QFileInfo(QString("/proc/%1/exe").arg(source.processId)).symLinkTarget();
	source.icon = windowIcon(source.owner);
	addIdentity(connection, source.owner, source);
	const auto client = connection.client(source.owner);
	const auto leader = connection.number(source.owner, leaderAtom, XCB_ATOM_WINDOW);
	auto windows = connection.windows(connection.atom("_NET_CLIENT_LIST"));
	/* Prefer the active window only after verifying its source identity. */
	const auto active = connection.number(connection.root, connection.atom("_NET_ACTIVE_WINDOW"), XCB_ATOM_WINDOW);
	if (active && windows.removeAll(active)) windows.prepend(active);
	if (leader && !windows.contains(leader)) windows.prepend(leader);
	for (xcb_window_t window : windows) {
		if (window == source.owner) continue;
		bool related = client.resource_base && (window & ~client.resource_mask) == client.resource_base;
		if (!related && leader)
			related = window == leader || connection.number(window, leaderAtom, XCB_ATOM_WINDOW) == leader;
		if (!related && source.processId) {
			if (nativePid) related = connection.processId(window) == nativePid;
			else related = !machine.isEmpty() && connection.text(window, machineAtom) == machine &&
				connection.number(window, pidAtom, XCB_ATOM_CARDINAL) == source.processId;
		}
		if (!related) continue;
		addIdentity(connection, window, source);
		if (source.icon.isNull()) source.icon = windowIcon(window);
		if (!source.icon.isNull()) break;
	}
	/* Never associate a new selection with metadata from the previous owner. */
	auto current = reply(xcb_get_selection_owner_reply(connection.get(),
		xcb_get_selection_owner(connection.get(), clipboard), nullptr));
	return current && current->owner == source.owner ? source : Source();
}

QString LinuxClipboardIcon::desktopIcon(const Source &source)
{
	int bestScore = 0;
	QString bestIcon;
	bool ambiguous = false;
	GList *applications = g_app_info_get_all();
	for (GList *i = applications; i; i = i->next) {
		if (!G_IS_DESKTOP_APP_INFO(i->data)) continue;
		auto *info = G_DESKTOP_APP_INFO(i->data);
		if (g_desktop_app_info_get_is_hidden(info)) continue;
		int score = 0;
		const QString id = desktopId(QString::fromUtf8(g_app_info_get_id(G_APP_INFO(info))));
		for (const QString &candidate : source.desktopIds)
			if (desktopId(candidate) == id) score = 400;
		const QString startupClass = QString::fromUtf8(g_desktop_app_info_get_startup_wm_class(info));
		for (const QString &name : source.classes) {
			if (!startupClass.isEmpty() && name.compare(startupClass, Qt::CaseInsensitive) == 0)
				score = qMax(score, 300);
			if (desktopId(name) == id) score = qMax(score, 200);
		}
		if (!score && !source.executable.isEmpty()) {
			QString executable = QString::fromUtf8(g_app_info_get_executable(G_APP_INFO(info)));
			if (!executable.isEmpty() && !QDir::isAbsolutePath(executable))
				executable = QStandardPaths::findExecutable(executable);
			if (!executable.isEmpty() && QFileInfo(executable).canonicalFilePath() == source.executable)
				score = 100;
		}
		if (!score || score < bestScore) continue;
		gchar *value = g_desktop_app_info_get_string(info, "Icon");
		const QString icon = QString::fromUtf8(value);
		g_free(value);
		if (score > bestScore) {
			bestScore = score;
			bestIcon = icon;
			ambiguous = false;
		} else if (icon != bestIcon) ambiguous = true;
	}
	g_list_free_full(applications, g_object_unref);
	return ambiguous ? QString() : bestIcon;
}
