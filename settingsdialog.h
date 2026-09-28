#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include "appdialog.h"
#include "platform/startupintegration.h"

class QLabel;
class QButtonGroup;
class QVariant;

class SettingsDialog : public AppDialog
{
	Q_OBJECT
public:
	SettingsDialog(const QString &shortcut, QWidget *parent = nullptr);
	void setPrimaryShortcut(const QString &shortcut);

signals:
	void themeChanged(const QString &theme);
	void recordingChanged(bool enabled);
	void hintsChanged(bool visible);

private:
	bool savePreference(const QString &key, const QVariant &value);
	void showError(const QString &error);
	void updateThemeChoices(void);
	StartupIntegration m_startup;
	QLabel *m_status;
	QLabel *m_shortcut;
	QButtonGroup *m_themes;
};

#endif
