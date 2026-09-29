#ifndef PASTES_SECRETSTORE_H
#define PASTES_SECRETSTORE_H
#include "application/syncservice.h"
#include <memory>
namespace Platform {
std::unique_ptr<SecretStore> createSecretStore(void);
}
#endif
