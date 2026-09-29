#include <QApplication>
#include <QGuiApplication>
#include <SingleApplication>
#include <QTranslator>
#include <QLocale>
#include <QTimer>
#include <QSettings>
#include <QDebug>
#include <QStandardPaths>
#include "sync/webdavsync.h"
#include "platform/secretstore.h"

#include "ui/mainwindow.h"
#include "application/clipboardcontroller.h"
#include "storage/database.h"
#include "platform/clipboardsource.h"
#include "platform/applicationintegration.h"
#include "platform/paths.h"

void LoadTranlateFile(SingleApplication *app)
{
	QTranslator *translator = new QTranslator(app);

	QLocale locale = QLocale::system();
	if (locale.language() == QLocale::Chinese) {
		const QString directory = Platform::translationDirectory();
		if (translator->load(directory+"/Pastes_zh_CN.qm") || translator->load("Pastes_zh_CN.qm"))
			app->installTranslator(translator);
	}
}

int main(int argc, char *argv[])
{
	QCoreApplication::setOrganizationName("JackieLiu");
	QCoreApplication::setApplicationName("Pastes");

	SingleApplication a(argc, argv);
	Platform::configureApplication();
	LoadTranlateFile(&a);

	/* Construction order is the lifetime graph: views disappear before
	 * services, then the repository drains its independent worker queue. */
	Database repository(Platform::databasePath());
	QObject::connect(&repository, &HistoryRepository::failed, &a, [](const QString &message) {
		qWarning() << "Pastes database:" << message;
	});
	HistoryService history(repository);
	ClipboardSource source;
	ClipboardController clipboard(history, source, *QGuiApplication::clipboard(),
		!QSettings().value("pauseRecording", false).toBool());
	auto secrets = Platform::createSecretStore();
	WebDavSync sync(history, *secrets, QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/sync");
	MainWindow w(history, clipboard, nullptr, &sync);
	history.load();
	const auto &behavior = Platform::applicationBehavior();
	QObject::connect(&a, &SingleApplication::instanceStarted, &w, [&w, behavior](void) {
		if (behavior.showOnSecondInstance) w.show_window();
		else w.hide();
	});
	if (behavior.showOnActivation) {
		QObject::connect(&a, &QGuiApplication::applicationStateChanged, &w,
			[&w](Qt::ApplicationState state) {
			if (state == Qt::ApplicationActive && !w.isVisible())
				w.show_window();
		});
	}
	if (behavior.showOnLaunch || a.arguments().contains(QStringLiteral("--show")))
		QTimer::singleShot(0, &w, &MainWindow::show_window);

	a.setQuitOnLastWindowClosed(false);
	return a.exec();
}
