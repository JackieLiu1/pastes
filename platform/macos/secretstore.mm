#include "platform/secretstore.h"
#import <Foundation/Foundation.h>
#import <Security/Security.h>

namespace {
NSDictionary *query(const QString &account)
{
	return @{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
		(__bridge id)kSecAttrService: @"io.github.JackieLiu1.pastes.webdav",
		(__bridge id)kSecAttrAccount: account.toNSString()};
}
QString failure(OSStatus status)
{
	CFStringRef message = SecCopyErrorMessageString(status, nullptr);
	const QString detail = message ? QString::fromCFString(message) : QString::number(status);
	if (message) CFRelease(message);
	return QObject::tr("Could not access the system credential store: %1").arg(detail);
}
class KeychainStore final : public SecretStore
{
public:
	QString read(const QString &account, QString *error) override
	{
		@autoreleasepool {
			NSMutableDictionary *request = [query(account) mutableCopy];
			request[(__bridge id)kSecReturnData] = @YES;
			CFTypeRef value = nullptr;
			OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)request, &value);
			[request release];
			if (status == errSecItemNotFound) return {};
			if (status != errSecSuccess) { *error = failure(status); return {}; }
			NSData *data = (__bridge NSData *)value;
			QString secret = QString::fromUtf8(static_cast<const char *>(data.bytes), data.length);
			CFRelease(value);
			return secret;
		}
	}
	bool write(const QString &account, const QString &secret, QString *error) override
	{
		@autoreleasepool {
			const QByteArray bytes = secret.toUtf8();
			NSData *value = [NSData dataWithBytes:bytes.constData() length:bytes.size()];
			NSDictionary *attributes = @{(__bridge id)kSecValueData: value};
			OSStatus status = SecItemUpdate((__bridge CFDictionaryRef)query(account),
				(__bridge CFDictionaryRef)attributes);
			if (status == errSecItemNotFound) {
				NSMutableDictionary *request = [query(account) mutableCopy];
				[request addEntriesFromDictionary:attributes];
				status = SecItemAdd((__bridge CFDictionaryRef)request, nullptr);
				[request release];
			}
			if (status != errSecSuccess) *error = failure(status);
			return status == errSecSuccess;
		}
	}
};
}
std::unique_ptr<SecretStore> Platform::createSecretStore(void)
{
	return std::make_unique<KeychainStore>();
}
