#include "platform/secretstore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr qint64 maxFileBytes = 64*1024;
constexpr auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;

class LocalCredentialStore final : public SecretStore
{
public:
	explicit LocalCredentialStore(const QString &directory) : m_directory(directory) {}

	QString read(const QString &account, QString *error) override
	{
		const QString path = filename(account);
		if (!QFileInfo::exists(path)) {
			*error = QObject::tr("Save your WebDAV password once in Settings → Sync to enable automatic sync on this device.");
			return {};
		}
		QFile file(path);
		if (!privateDirectory(error) || QFileInfo(path).isSymLink() ||
			!file.setPermissions(filePermissions) || !file.open(QIODevice::ReadOnly)) {
			*error = QObject::tr("Could not read the locally saved WebDAV password.");
			return {};
		}
		const QByteArray bytes = file.read(maxFileBytes+1);
		QJsonParseError parseError;
		const auto document = QJsonDocument::fromJson(bytes, &parseError);
		const auto object = document.object();
		if (bytes.size() > maxFileBytes || file.error() != QFileDevice::NoError ||
			parseError.error != QJsonParseError::NoError || !document.isObject() ||
			object.value("version").toInt() != 1 || !object.value("password").isString()) {
			*error = QObject::tr("The locally saved WebDAV password is damaged. Save it again in Settings → Sync.");
			return {};
		}
		return object.value("password").toString();
	}

	bool write(const QString &account, const QString &secret, QString *error) override
	{
		const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"password", secret}})
			.toJson(QJsonDocument::Compact);
		if (bytes.size() > maxFileBytes) {
			*error = QObject::tr("The WebDAV password is too long to save.");
			return false;
		}
		if (!privateDirectory(error)) return false;
		const QString path = filename(account);
		QSaveFile file(path);
		file.setDirectWriteFallback(false);
		/* Restrict the temporary file before writing any credential bytes. */
		if (QFileInfo(path).isSymLink() || !file.open(QIODevice::WriteOnly) ||
			!file.setPermissions(filePermissions) || file.write(bytes) != bytes.size() || !file.commit()) {
			*error = QObject::tr("Could not save the WebDAV password locally.");
			return false;
		}
		return true;
	}

private:
	QString filename(const QString &account) const
	{
		/* Account identifiers never become path components verbatim. */
		const auto name = QCryptographicHash::hash(account.toUtf8(), QCryptographicHash::Sha256).toHex();
		return m_directory+'/'+QString::fromLatin1(name)+".json";
	}
	bool privateDirectory(QString *error) const
	{
		if (QFileInfo(m_directory).isSymLink() || !QDir().mkpath(m_directory) ||
			!QFile::setPermissions(m_directory, filePermissions | QFileDevice::ExeOwner)) {
			*error = QObject::tr("Could not create a private folder for the WebDAV password.");
			return false;
		}
		return true;
	}
	QString m_directory;
};
}

std::unique_ptr<SecretStore> Platform::createLocalSecretStore(const QString &directory)
{
	return std::make_unique<LocalCredentialStore>(directory);
}
