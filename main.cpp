#include <QApplication>
#include <QGuiApplication>
#include <SingleApplication>
#include <QTranslator>
#include <QLocale>
#include <QTimer>
#include <QIcon>

#include "mainwindow.h"
#ifdef Q_OS_MACOS
#include "window_mac.h"
#endif

#ifndef QM_FILES_INSTALL_PATH
#define QM_FILES_INSTALL_PATH "."
#endif

void LoadTranlateFile(SingleApplication *app)
{
	QTranslator *translator = new QTranslator(app);

	QLocale locale = QLocale::system();
	if (locale.language() == QLocale::Chinese) {
		QString directory = QString(QM_FILES_INSTALL_PATH);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
		/* Startup entries do not guarantee the executable's working directory. */
		directory = QCoreApplication::applicationDirPath();
#endif
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
#ifdef Q_OS_MACOS
	a.setWindowIcon(QIcon(":/resources/pastes.svg"));
	configureMacApplication();
#endif
	LoadTranlateFile(&a);

	MainWindow w;
#ifdef Q_OS_MACOS
	QObject::connect(&a, &SingleApplication::instanceStarted,
			 &w, &MainWindow::show_window);
	QObject::connect(&a, &QGuiApplication::applicationStateChanged, &w,
		[&w](Qt::ApplicationState state) {
		if (state == Qt::ApplicationActive && !w.isVisible())
			w.show_window();
	});
	QTimer::singleShot(0, &w, &MainWindow::show_window);
#else
	QObject::connect(&a, &SingleApplication::instanceStarted, [&w](void) {
		w.hide();
	});
	if (a.arguments().contains(QStringLiteral("--show")))
		QTimer::singleShot(0, &w, &MainWindow::show_window);
#endif

	a.setQuitOnLastWindowClosed(false);
	return a.exec();
}
