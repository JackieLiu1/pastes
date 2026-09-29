#include "tests/testsupport.h"
#include "ui/settingsdialog.h"
#include "ui/syncsettingspage.h"
#include "application/syncservice.h"
#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTranslator>

class FakeSync final : public SyncService
{
public:
	SyncSettings settings(void) const override { return config; }
	bool save(const SyncSettings &value, const QString &secret, QString *) override { config = value; password = secret; ++saves; return true; }
	void synchronize(void) override { ++runs; }
	void testConnection(void) override { ++probes; }
	bool busy(void) const override { return running; }
	QString status(void) const override { return QCoreApplication::translate("WebDavSync", "Ready to sync."); }
	QDateTime lastSuccess(void) const override { return {}; }
	SyncSettings config;
	QString password;
	int saves = 0, runs = 0, probes = 0;
	bool running = false;
};
int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir preferences;
	QCoreApplication::setOrganizationName("PastesSettingsTests");
	QCoreApplication::setApplicationName("Settings");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
	QTranslator translator;
	if (app.arguments().contains("--render") && (translator.load(QCoreApplication::applicationDirPath()+"/../Resources/Pastes_zh_CN.qm") || translator.load("Pastes_zh_CN.qm"))) app.installTranslator(&translator);
	return runTest("settings tabs and sync controls", [&] {
		FakeSync sync;
		SettingsDialog dialog("Shift+Cmd+V", nullptr, &sync);
		auto *tabs = dialog.findChild<QTabWidget *>("SettingsTabs");
		require(tabs && tabs->count() == 3, "Settings tabs are missing");
		tabs->setCurrentIndex(2);
		auto *password = dialog.findChild<QLineEdit *>("SyncPassword");
		require(password && password->echoMode() == QLineEdit::Password, "Password must be masked");
		dialog.findChild<QLineEdit *>("SyncAddress")->setText("https://dav.example.com/history/");
		dialog.findChild<QLineEdit *>("SyncUsername")->setText("user@example.com");
		password->setText("test-secret");
		dialog.findChild<QCheckBox *>("SyncEnabled")->setChecked(true);
		const auto buttons = dialog.findChildren<QPushButton *>("SyncAction");
		require(buttons.size() == 3, "Missing sync action buttons");
		buttons[0]->click();
		require(sync.saves == 1 && sync.password == "test-secret" && password->text().isEmpty(), "Saving did not clear the password field");
		buttons[1]->click(); require(sync.probes == 1, "Connection test was not dispatched");
		buttons[2]->click(); require(sync.runs == 1, "Manual sync was not dispatched");
		sync.running = true; emit sync.statusChanged();
		for (const auto *button : buttons) require(!button->isEnabled(), "Actions remain enabled during a run");
		sync.running = false; emit sync.statusChanged();
		if (app.arguments().contains("--render")) {
			for (const QString &theme : {QString("light"),QString("dark")}) {
				QFile file(":/resources/theme-"+theme+".qss"); require(file.open(QIODevice::ReadOnly), "Theme missing");
				app.setProperty("pastesDark", theme == "dark"); app.setStyleSheet(QString::fromUtf8(file.readAll()));
				dialog.show();
				for (int index = 0; index < tabs->count(); ++index) {
					tabs->setCurrentIndex(index);
					QEventLoop loop; QTimer::singleShot(100, &loop, &QEventLoop::quit); loop.exec();
					const int directoryArg = app.arguments().indexOf("--render-dir");
					const QString directory = directoryArg < 0 ? QDir::currentPath() : app.arguments().value(directoryArg+1);
					require(dialog.grab().save(directory+"/settings-"+theme+"-"+QString::number(index)+".png"), "Screenshot failed");
				}
			}
		}
		if (app.arguments().contains("--interactive")) {
			dialog.show();
			QTimer::singleShot(180000, &app, &QCoreApplication::quit);
			app.exec();
		}
	});
}
