#ifndef PASTES_WEBDAVWORKER_H
#define PASTES_WEBDAVWORKER_H
#include "application/syncservice.h"
#include "sync/synccontent.h"
#include <QHash>
#include <QSet>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
struct SyncDelivery {
	QString id;
	QString key;
	EventState events;
	SyncContent::Snapshot content;
	QList<QByteArray> replaced;
	bool deleted = false;
};
Q_DECLARE_METATYPE(SyncDelivery)

class WebDavWorker final : public QObject
{
	Q_OBJECT
public:
	void configure(const SyncSettings &settings, const QString &password, const QString &directory,
		bool committedOnly = true);
	void accept(QByteArray document, bool pending = true, const QString &knownId = {});
	void run(bool probe);
	void stop(void);
	void acknowledge(const QString &id) { m_delivered.insert(id); }
signals:
	void uploaded(QString id);
	void delivered(const SyncDelivery &delivery);
	void finished(const QString &message, bool success, bool probe);
	void warning(const QString &message);

private:
	using Response = std::function<void(int, const QByteArray &)>;
	void request(const QByteArray &method, const QString &relative, const QByteArray &body, Response done);
	void ensureDirectory(int index);
	void list(void);
	void downloadNext(void);
	void uploadNext(void);
	bool prepareRetention(void);
	void cleanupNext(void);
	void deliver(void);
	void complete(const QString &error = {});
	bool persistIndex(void);
	bool store(const QByteArray &bytes, bool pending);
	QByteArray read(const QString &id);
	void remember(SyncContent::Record record);
	QString path(const QString &id) const;

	SyncSettings m_settings;
	QString m_password;
	QString m_directory;
	QString m_error;
	QHash<QString, SyncContent::Record> m_records;
	QHash<QString, QString> m_heads;
	QHash<QByteArray, QString> m_bindings;
	QSet<QString> m_pending;
	QSet<QString> m_local;
	QSet<QString> m_delivered;
	QStringList m_downloads, m_uploads, m_cleanup;
	QNetworkAccessManager *m_network = nullptr;
	QNetworkReply *m_reply = nullptr;
	bool m_running = false;
	bool m_probe = false;
	bool m_committedOnly = false;
	QSet<QString> m_durable, m_remote;
};
#endif
