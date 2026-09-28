#ifndef STARTUPINTEGRATION_H
#define STARTUPINTEGRATION_H

#include <QString>

/* Startup state comes from the OS entry, rather than a preference that can
 * drift from actual registration. macOS manages the calling app bundle;
 * desktop entry names allow isolated QA on Windows and Linux. */
class StartupIntegration
{
public:
	explicit StartupIntegration(const QString &executable = QString(), const QString &entryName = QStringLiteral("Pastes"));
	bool supported(void) const;
	bool enabled(void) const;
	QString statusMessage(void) const;
	bool setEnabled(bool enabled, QString *error);

private:
	bool setEnabledNative(bool enabled, QString *error);
	QString m_executable;
	QString m_entry_name;
};

#endif
