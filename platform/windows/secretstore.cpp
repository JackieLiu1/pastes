#include "platform/secretstore.h"
#include <windows.h>
#include <wincred.h>

namespace {
class CredentialStore final : public SecretStore
{
public:
	QString read(const QString &account, QString *error) override
	{
		const QString target = "Pastes/WebDAV/"+account;
		PCREDENTIALW value = nullptr;
		if (!CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &value)) {
			if (GetLastError() != ERROR_NOT_FOUND)
				*error = QObject::tr("Could not read Windows Credential Manager.");
			return {};
		}
		const QString secret = QString::fromUtf8(reinterpret_cast<const char *>(value->CredentialBlob), value->CredentialBlobSize);
		CredFree(value);
		return secret;
	}
	bool write(const QString &account, const QString &secret, QString *error) override
	{
		QString target = "Pastes/WebDAV/"+account;
		QByteArray bytes = secret.toUtf8();
		CREDENTIALW credential{};
		credential.Type = CRED_TYPE_GENERIC;
		credential.TargetName = reinterpret_cast<LPWSTR>(target.data());
		credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
		credential.CredentialBlob = reinterpret_cast<LPBYTE>(bytes.data());
		credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
		if (CredWriteW(&credential, 0)) return true;
		*error = QObject::tr("Could not save to Windows Credential Manager.");
		return false;
	}
};
}
std::unique_ptr<SecretStore> Platform::createSecretStore(void)
{
	return std::make_unique<CredentialStore>();
}
