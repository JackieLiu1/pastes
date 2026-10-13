#include "tests/testsupport.h"
#include "platform/clipboardsource.h"
#include "platform/linux/clipboardicon.h"
#include "application/clipboardcontroller.h"
#include "storage/database.h"
#include "core/sourceicon.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QIcon>
#include <QProcess>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QWidget>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <xcb/xcb.h>
#include <xcb/res.h>

namespace {

QString fixtureRoot;
constexpr QRgb red = 0xffff2244;
constexpr QRgb blue = 0xff2266ff;
constexpr char marker[] = "Pastes native Linux clipboard icon smoke";

QImage coloredIcon(QRgb color)
{
	QImage image(128, 128, QImage::Format_ARGB32);
	image.fill(color);
	return image;
}

QList<quint32> iconWords(int size, QRgb color)
{
	QList<quint32> words(size*size+2, color);
	words[0] = words[1] = quint32(size);
	return words;
}

void writeFile(const QString &name, const QByteArray &data)
{
	QFile file(name);
	require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Could not write icon fixture");
}

void fixtures(void)
{
	const QString applications = fixtureRoot+"/data/applications";
	const QString theme = fixtureRoot+"/icons/PastesFixtureTheme";
	QDir().mkpath(applications);
	QDir().mkpath(theme+"/128x128/apps");
	require(coloredIcon(red).save(fixtureRoot+"/red.png") &&
		coloredIcon(blue).save(theme+"/128x128/apps/pastes-other.png"), "Could not save icon fixtures");
	writeFile(theme+"/index.theme", "[Icon Theme]\nName=PastesFixtureTheme\nDirectories=128x128/apps\n"
		"[128x128/apps]\nSize=128\nType=Fixed\nContext=Applications\n");
	writeFile(applications+"/org.pastes.IconFixture.desktop", "[Desktop Entry]\nType=Application\n"
		"Name=Source fixture\nExec=/usr/bin/true\nStartupWMClass=PastesIconFixture\nIcon="+
		(fixtureRoot+"/red.png").toUtf8()+"\n");
	writeFile(applications+"/org.pastes.Other.desktop", "[Desktop Entry]\nType=Application\n"
		"Name=Other fixture\nExec=/usr/bin/true\nStartupWMClass=PastesOther\nIcon=pastes-other\n");
	writeFile(applications+"/org.pastes.NoIcon.desktop", "[Desktop Entry]\nType=Application\n"
		"Name=No icon fixture\nExec=/usr/bin/true\n");
	QIcon::setThemeSearchPaths({fixtureRoot+"/icons"});
	QIcon::setThemeName("PastesFixtureTheme");
}

void iconDecoding(void)
{
	auto words = iconWords(16, red)+iconWords(64, red)+iconWords(256, blue)+iconWords(128, blue);
	QImage image = LinuxClipboardIcon::decodeIcon(words.constData(), words.size());
	require(image.size() == QSize(128, 128) && image.pixel(0, 0) == blue,
		"Did not select the suitable HiDPI icon from multiple sizes");
	words = iconWords(16, red)+iconWords(64, blue);
	image = LinuxClipboardIcon::decodeIcon(words.constData(), words.size());
	require(image.size() == QSize(64, 64) && image.pixel(0, 0) == blue, "Small icons were enlarged or lost");
	words = iconWords(256, red);
	image = LinuxClipboardIcon::decodeIcon(words.constData(), words.size());
	require(image.size() == QSize(128, 128), "Large source icon was not bounded");
	for (const QList<quint32> &invalid : {QList<quint32>{}, {128}, {0, 128}, {128, 128, red},
		{std::numeric_limits<quint32>::max(), std::numeric_limits<quint32>::max(), red}})
		require(LinuxClipboardIcon::decodeIcon(invalid.constData(), invalid.size()).isNull(),
			"Malformed or overflowing icon dimensions were accepted");
	words = {1, 1, 0x80112233};
	image = LinuxClipboardIcon::decodeIcon(words.constData(), words.size());
	words[2] = blue;
	require(image.pixel(0, 0) == 0x80112233, "Icon lost alpha or retained borrowed property storage");
}

void desktopMatching(void)
{
	LinuxClipboardIcon::Source source;
	source.classes = {"PastesIconFixture"};
	require(LinuxClipboardIcon::desktopIcon(source) == fixtureRoot+"/red.png", "StartupWMClass did not match");
	source.classes = {"org.pastes.Other"};
	require(LinuxClipboardIcon::desktopIcon(source) == "pastes-other", "Desktop ID did not match WM_CLASS");
	source.desktopIds = {"org.pastes.IconFixture.desktop"};
	require(LinuxClipboardIcon::desktopIcon(source) == fixtureRoot+"/red.png", "Explicit desktop ID lost priority");
	source.desktopIds = {"org.pastes.NoIcon.desktop"};
	require(LinuxClipboardIcon::desktopIcon(source).isEmpty(), "An app without an icon borrowed another app's icon");
	source = {};
	source.executable = QFileInfo("/usr/bin/true").canonicalFilePath();
	require(LinuxClipboardIcon::desktopIcon(source).isEmpty(), "Shared executable was attributed to an arbitrary app");
	source.classes = {"UninstalledPastesFixture"};
	source.executable.clear();
	require(LinuxClipboardIcon::desktopIcon(source).isEmpty(), "Unknown application received another app's icon");
	require(QIcon::fromTheme("pastes-other").pixmap(128, 128).toImage().pixel(0, 0) == blue,
		"Fixture theme icon could not be rendered");
}

struct NativeWindows
{
	std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection{xcb_connect(nullptr, nullptr), &xcb_disconnect};
	xcb_window_t root = xcb_setup_roots_iterator(xcb_get_setup(connection.get())).data->root;
	xcb_atom_t clipboard = atom("CLIPBOARD"), icon = atom("_NET_WM_ICON");
	xcb_atom_t clientList = atom("_NET_CLIENT_LIST"), active = atom("_NET_ACTIVE_WINDOW");
	xcb_window_t owner = window(), main = XCB_NONE;

	NativeWindows(void)
	{
		/* Both windows lack titles, WM_CLASS, leaders and PID properties. */
		for (int i = 0; i < 600; ++i) xcb_generate_id(connection.get());
		main = window();
		setIcon(main, red);
		own();
	}
	~NativeWindows(void)
	{
		xcb_delete_property(connection.get(), root, clientList);
		xcb_delete_property(connection.get(), root, active);
		flush();
	}
	xcb_atom_t atom(const char *name)
	{
		auto *value = xcb_intern_atom_reply(connection.get(),
			xcb_intern_atom(connection.get(), false, uint16_t(qstrlen(name)), name), nullptr);
		require(value, "Could not intern native fixture atom");
		const auto result = value->atom;
		std::free(value);
		return result;
	}
	xcb_window_t window(void)
	{
		const auto id = xcb_generate_id(connection.get());
		xcb_create_window(connection.get(), XCB_COPY_FROM_PARENT, id, root, 0, 0, 32, 32,
			0, XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr);
		return id;
	}
	void setIcon(xcb_window_t window, QRgb color)
	{
		const auto words = iconWords(128, color);
		xcb_change_property(connection.get(), XCB_PROP_MODE_REPLACE, window, icon,
			XCB_ATOM_CARDINAL, 32, uint32_t(words.size()), words.constData());
	}
	void own(void) { xcb_set_selection_owner(connection.get(), owner, clipboard, XCB_CURRENT_TIME); flush(); }
	void flush(void)
	{
		auto *value = xcb_get_input_focus_reply(connection.get(), xcb_get_input_focus(connection.get()), nullptr);
		std::free(value);
	}
};

struct QtSource
{
	QProcess process;
	xcb_window_t main = XCB_NONE;
	QtSource(void)
	{
		process.start(QCoreApplication::applicationFilePath(), {"-platform", "xcb", "--qt-source"});
		waitUntil([&] { return process.canReadLine() || process.state() == QProcess::NotRunning; });
		const auto line = process.readLine().trimmed();
		bool valid = false;
		main = line.toUInt(&valid);
		if (!valid) qWarning() << process.readAllStandardError();
		require(valid && main, "Qt clipboard source failed to start");
	}
	~QtSource(void) { process.kill(); process.waitForFinished(2000); }
};

bool nativeTests(void)
{
	if (QGuiApplication::platformName() == "xcb" && qApp->arguments().contains("--isolated-x11")) return true;
	qInfo() << "SKIP native clipboard icons: use an isolated X11 display and --isolated-x11";
	return false;
}

void namelessOwnerAndLateResults(void)
{
	if (!nativeTests()) return;
	QtSource unrelated;
	NativeWindows windows;
	const xcb_window_t candidates[] = {0xdeadbeef, unrelated.main, windows.main};
	xcb_change_property(windows.connection.get(), XCB_PROP_MODE_REPLACE, windows.root, windows.clientList,
		XCB_ATOM_WINDOW, 32, 3, candidates);
	xcb_change_property(windows.connection.get(), XCB_PROP_MODE_REPLACE, windows.root, windows.active,
		XCB_ATOM_WINDOW, 32, 1, &unrelated.main);
	windows.flush();
	ClipboardSource source;
	QHash<quint64, QImage> results;
	QObject::connect(&source, &ClipboardFeed::iconReady, &source,
		[&](quint64 request, const QImage &image) { results.insert(request, image); });
	source.capture(100);
	const auto *extension = xcb_get_extension_data(windows.connection.get(), &xcb_res_id);
	if (extension && extension->present) {
		require(results.contains(100) && results[100].pixel(0, 0) == red,
			"Nameless selection owner was not associated with its own application window");
		const auto snapshot = LinuxClipboardIcon::captureSource();
		require(snapshot.processId == quint32(QCoreApplication::applicationPid()), "XRes did not recover owner PID");
	} else require(results.contains(100) && results[100].isNull(), "Unknown owner borrowed the foreground app's icon");
	const auto leader = windows.atom("WM_CLIENT_LEADER");
	xcb_change_property(windows.connection.get(), XCB_PROP_MODE_REPLACE, windows.owner, leader,
		XCB_ATOM_WINDOW, 32, 1, &windows.main);
	windows.flush();
	source.capture(99);
	require(results.contains(99) && results[99].pixel(0, 0) == red, "Client-leader hint did not associate the owner");
	xcb_destroy_window(windows.connection.get(), windows.main);
	windows.setIcon(windows.owner, blue);
	windows.flush();
	require(source.snapshotIcon().pixel(0, 0) == red, "Snapshot was re-read after the original window disappeared");
	source.capture(101);
	require(results[101].pixel(0, 0) == blue, "Direct selection-owner icon was lost");

	xcb_delete_property(windows.connection.get(), windows.owner, windows.icon);
	constexpr char classNames[] = "pastes-fixture\0PastesIconFixture";
	const QByteArray classes(classNames, sizeof(classNames));
	xcb_change_property(windows.connection.get(), XCB_PROP_MODE_REPLACE, windows.owner, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, uint32_t(classes.size()), classes.constData());
	windows.flush();
	source.capture(200);
	windows.setIcon(windows.owner, blue);
	windows.flush();
	source.capture(201);
	waitUntil([&] { return results.contains(200); });
	require(results[200].pixel(0, 0) == red && source.snapshotIcon().pixel(0, 0) == blue,
		"Desktop fallback failed or a late result overwrote the newer source");
	xcb_delete_property(windows.connection.get(), windows.owner, windows.icon);
	constexpr char otherNames[] = "pastes-other\0PastesOther";
	const QByteArray other(otherNames, sizeof(otherNames));
	xcb_change_property(windows.connection.get(), XCB_PROP_MODE_REPLACE, windows.owner, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, uint32_t(other.size()), other.constData());
	windows.flush();
	source.capture(202);
	waitUntil([&] { return results.contains(202); });
	require(results[202].pixel(0, 0) == blue, "Desktop theme fallback did not deliver a source icon");
	for (int i = 0; i < 10; ++i) { ClipboardSource pending; pending.capture(300+i); }
}

void nativeHistorySmoke(void)
{
	if (!nativeTests()) return;
	const QString path = fixtureRoot+"/history.db";
	QImage capturedIcon;
	{
		Database repository(path);
		HistoryService history(repository);
		ClipboardSource source;
		ClipboardController controller(history, source, *QApplication::clipboard(), true);
		bool loaded = false;
		QObject::connect(&repository, &HistoryRepository::loaded, &repository, [&] { loaded = true; });
		history.load();
		waitUntil([&] { return loaded; });
		QtSource child;
		waitUntil([&] { return history.entries().size() == 1; }, 5000);
		const auto entry = history.entries().first();
		require(entry->mimeData->text() == marker, "Native Qt clipboard capture lost text");
		std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(xcb_connect(nullptr, nullptr), &xcb_disconnect);
		const auto *extension = xcb_get_extension_data(connection.get(), &xcb_res_id);
		if (extension && extension->present)
			require(entry->icon.size() == QSize(128, 128) && entry->icon.pixel(0, 0) == blue,
				"Native Qt clipboard capture lost the source icon");
		capturedIcon = entry->icon;
	}
	Database restarted(path);
	QList<HistoryEntry> entries;
	bool loaded = false;
	QObject::connect(&restarted, &HistoryRepository::loaded, &restarted, [&](const QList<HistoryEntry> &value) {
		entries = value; loaded = true;
	});
	restarted.load();
	waitUntil([&] { return loaded; });
	require(entries.size() == 1 && entries.first()->mimeData->text() == marker &&
		entries.first()->icon == capturedIcon, "Restarted database lost native clipboard text or icon");
}

void separateConnectionsAndHints(void)
{
	if (!nativeTests()) return;
	NativeWindows owner, visible;
	owner.own();
	const auto connection = owner.connection.get();
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, owner.root, owner.clientList,
		XCB_ATOM_WINDOW, 32, 1, &visible.main);
	const auto pidAtom = owner.atom("_NET_WM_PID");
	const auto machineAtom = owner.atom("WM_CLIENT_MACHINE");
	const quint32 pid = quint32(QCoreApplication::applicationPid());
	const QByteArray machine = QSysInfo::machineHostName().toUtf8();
	for (xcb_window_t window : {owner.owner, visible.main}) {
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, pidAtom, XCB_ATOM_CARDINAL, 32, 1, &pid);
		xcb_change_property(connection, XCB_PROP_MODE_REPLACE, window, machineAtom,
			XCB_ATOM_STRING, 8, uint32_t(machine.size()), machine.constData());
	}
	owner.flush();
	require(LinuxClipboardIcon::captureSource().icon.pixel(0, 0) == red,
		"Separate X11 connections from the same process were not associated");
	xcb_delete_property(connection, owner.owner, pidAtom);
	xcb_delete_property(connection, owner.owner, machineAtom);
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, owner.owner, owner.atom("WM_CLIENT_LEADER"),
		XCB_ATOM_WINDOW, 32, 1, &visible.main);
	owner.flush();
	require(LinuxClipboardIcon::captureSource().icon.pixel(0, 0) == red,
		"Client leader could not associate windows on separate X11 connections");
}

int qtSource(int argc, char **argv)
{
	QApplication app(argc, argv);
	QWidget window;
	window.setWindowTitle(QString());
	window.setWindowIcon(QIcon(QPixmap::fromImage(coloredIcon(blue))));
	window.show();
	QTimer::singleShot(0, &app, [&] {
		QApplication::clipboard()->setText(marker);
		std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection(xcb_connect(nullptr, nullptr), &xcb_disconnect);
		auto atom = [&](const char *name) {
			auto *value = xcb_intern_atom_reply(connection.get(),
				xcb_intern_atom(connection.get(), false, uint16_t(qstrlen(name)), name), nullptr);
			const auto result = value ? value->atom : xcb_atom_t(XCB_NONE);
			std::free(value);
			return result;
		};
		auto *owner = xcb_get_selection_owner_reply(connection.get(),
			xcb_get_selection_owner(connection.get(), atom("CLIPBOARD")), nullptr);
		if (owner && owner->owner) {
			const xcb_atom_t names[] = {XCB_ATOM_WM_NAME, XCB_ATOM_WM_CLASS,
				atom("_NET_WM_NAME"), atom("_NET_WM_PID")};
			for (xcb_atom_t name : names) xcb_delete_property(connection.get(), owner->owner, name);
		}
		std::free(owner);
		auto *sync = xcb_get_input_focus_reply(connection.get(), xcb_get_input_focus(connection.get()), nullptr);
		std::free(sync);
		std::printf("%u\n", unsigned(window.winId()));
		std::fflush(stdout);
	});
	return app.exec();
}

}

int main(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
		if (QByteArray(argv[i]) == "--qt-source") return qtSource(argc, argv);
	QTemporaryDir directory;
	fixtureRoot = directory.path();
	qputenv("XDG_DATA_HOME", (fixtureRoot+"/data").toUtf8());
	qputenv("XDG_DATA_DIRS", (fixtureRoot+"/system-data").toUtf8());
	qputenv("XDG_CONFIG_HOME", (fixtureRoot+"/config").toUtf8());
	qputenv("XDG_CACHE_HOME", (fixtureRoot+"/cache").toUtf8());
	QApplication app(argc, argv);
	fixtures();
	return runTest("Linux icon sizes, alpha and malformed properties", iconDecoding) |
		runTest("Linux desktop identity and ambiguous executable matches", desktopMatching) |
		runTest("nameless X11 owners, source lifetime and late icon requests", namelessOwnerAndLateResults) |
		runTest("X11 process identity and client-leader fallback across connections", separateConnectionsAndHints) |
		runTest("native Qt clipboard persistence and restart", nativeHistorySmoke);
}
