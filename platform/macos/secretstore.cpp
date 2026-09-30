#include "platform/secretstore.h"
#include <QStandardPaths>

std::unique_ptr<SecretStore> Platform::createSecretStore(void)
{
	return createLocalSecretStore(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		+"/credentials/webdav");
}
