#include <QApplication>
#include <QGuiApplication>
#include <SingleApplication>
#include <QTranslator>
#include <QLocale>
#include <QTimer>

#include "mainwindow.h"
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

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	/* On Qt6 high-DPI scaling is always on and these attributes are gone */
	QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

	SingleApplication a(argc, argv);
	Platform::configureApplication();
	LoadTranlateFile(&a);

	MainWindow w;
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
