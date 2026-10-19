#ifndef PASTES_WEBDAVSYNC_H
#define PASTES_WEBDAVSYNC_H
#include "application/syncservice.h"
#include "application/historyservice.h"
#include "sync/webdavworker.h"
#include <QSet>
#include <QThread>
#include <QTimer>

class WebDavSync final : public SyncService
{
	Q_OBJECT
public:
	WebDavSync(HistoryService &history, SecretStore &secrets, const QString &directory, QObject *parent = nullptr);
	~WebDavSync() override;
	SyncSettings settings(void) const override { return m_settings; }
	bool save(const SyncSettings &settings, const QString &password, QString *error) override;
	void synchronize(void) override;
	void testConnection(void) override;
	bool busy(void) const override { return m_busy; }
	QString status(void) const override { return m_status; }
	QDateTime lastSuccess(void) const override { return m_lastSuccess; }
	static bool validate(SyncSettings *settings, QString *error);
private:
	QString account(const SyncSettings &settings) const;
	void configure(void);
	void bootstrap(void);
	void run(bool probe);
	void finishRun(void);
	void schedule(void);
	HistoryService &m_history;
	SecretStore &m_secrets;
	SyncSettings m_settings;
	QString m_password, m_directory, m_status, m_warning;
	QDateTime m_lastSuccess;
	QThread m_thread;
	WebDavWorker *m_worker;
	QTimer m_timer;
	QSet<QString> m_applied, m_pendingApplies, m_pendingPublishes, m_loadedJournal;
	QString m_runError;
	bool m_transportDone = false, m_workerStarted = false;
	quint64 m_runToken = 0;
	quint64 m_epoch = 0;
	bool m_ready = false, m_configured = false, m_credentialsLoaded = false, m_busy = false, m_again = false, m_applying = false;
};
#endif
