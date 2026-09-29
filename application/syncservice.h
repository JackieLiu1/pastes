#ifndef PASTES_SYNCSERVICE_H
#define PASTES_SYNCSERVICE_H

#include <QObject>
#include <QDateTime>
#include <QUrl>

struct SyncSettings {
	QUrl url;
	QString username;
	bool enabled = false;
};

/* The UI knows this port, not the transport, credential backend or disk cache. */
class SyncService : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	virtual SyncSettings settings(void) const = 0;
	virtual bool save(const SyncSettings &settings, const QString &password, QString *error) = 0;
	virtual void synchronize(void) = 0;
	virtual void testConnection(void) = 0;
	virtual bool busy(void) const = 0;
	virtual QString status(void) const = 0;
	virtual QDateTime lastSuccess(void) const = 0;
signals:
	void statusChanged(void);
};

class SecretStore
{
public:
	virtual ~SecretStore() = default;
	virtual QString read(const QString &account, QString *error) = 0;
	virtual bool write(const QString &account, const QString &secret, QString *error) = 0;
};

#endif
