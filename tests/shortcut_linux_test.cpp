#include "tests/testsupport.h"
#include "platform/globalshortcut.h"
#include <QCoreApplication>
#include <QDir>
#include <X11/Xlib.h>
#include <X11/extensions/record.h>

namespace {

class DisplayEnvironment final
{
public:
	DisplayEnvironment(void) : m_present(qEnvironmentVariableIsSet("DISPLAY")), m_value(qgetenv("DISPLAY"))
	{
		qputenv("DISPLAY", QByteArray());
	}
	~DisplayEnvironment(void)
	{
		if (m_present) qputenv("DISPLAY", m_value);
		else qunsetenv("DISPLAY");
	}
private:
	bool m_present;
	QByteArray m_value;
};

int openDescriptors(void)
{
	return QDir("/proc/self/fd").entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
}

void missingDisplayAndEarlyStop(void)
{
	DisplayEnvironment environment;
	const int before = openDescriptors();
	{
		GlobalShortcut shortcut;
		QString primary;
		QObject::connect(&shortcut, &GlobalShortcut::primaryShortcutChanged, &shortcut,
			[&](const QString &value) { primary = value; });
		waitUntil([&] { return !primary.isEmpty(); });
		require(primary == QObject::tr("Tray icon"), "Missing X11 display did not fall back to the tray");
	}
	for (int i = 0; i < 50; ++i) {
		GlobalShortcut shortcut;
	}
	require(openDescriptors() == before, "Failed initialization or early shutdown leaked descriptors");
}

void recordingLifecycle(void)
{
	std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), &XCloseDisplay);
	int major = 0, minor = 0;
	if (!display || !XRecordQueryVersion(display.get(), &major, &minor)) {
		qInfo() << "SKIP X11 recording lifecycle: no display with RECORD support";
		return;
	}
	const int before = openDescriptors();
	/* Multiple instances must keep independent connections and contexts. */
	for (int i = 0; i < 10; ++i) {
		GlobalShortcut first, second;
		QString firstPrimary, secondPrimary;
		QObject::connect(&first, &GlobalShortcut::primaryShortcutChanged, &first,
			[&](const QString &value) { firstPrimary = value; });
		QObject::connect(&second, &GlobalShortcut::primaryShortcutChanged, &second,
			[&](const QString &value) { secondPrimary = value; });
		waitUntil([&] { return !firstPrimary.isEmpty() && !secondPrimary.isEmpty(); });
		require(firstPrimary == "Ctrl+Shift+V" && secondPrimary == "Ctrl+Shift+V",
			"X11 recording did not initialize for independent shortcut instances");
	}
	require(openDescriptors() == before, "Repeated X11 recording shutdown leaked display connections");
}

}

int main(int argc, char **argv)
{
	XInitThreads();
	QCoreApplication app(argc, argv);
	return runTest("missing X11 display and immediate shortcut destruction", missingDisplayAndEarlyStop) |
		runTest("independent X11 shortcut recording and repeated shutdown", recordingLifecycle);
}
