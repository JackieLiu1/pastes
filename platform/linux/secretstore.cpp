#include "platform/secretstore.h"
#include <QProcess>
#include <QStandardPaths>

namespace {
/* libsecret's CLI uses the desktop Secret Service and takes secrets on stdin,
 * never in process arguments or environment variables. */
class DesktopSecretStore final : public SecretStore
{
	bool run(const QStringList &args, const QByteArray &input, QByteArray *output, QString *error)
	{
		const QString program = QStandardPaths::findExecutable("secret-tool");
		if (program.isEmpty()) {
			*error = QObject::tr("Install secret-tool (libsecret) to save WebDAV credentials securely.");
			return false;
		}
		QProcess process;
		process.start(program, args);
		if (!process.waitForStarted(2000)) { *error = QObject::tr("Could not open the system credential store."); return false; }
		process.write(input); process.closeWriteChannel();
		if (!process.waitForFinished(5000)) { process.kill(); process.waitForFinished(); *error = QObject::tr("The system credential store did not respond."); return false; }
		*output = process.readAllStandardOutput();
		if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) return true;
		// A missing item is expected before the first save.
		if (args.first() == "lookup" && process.exitCode() == 1 && process.readAllStandardError().isEmpty()) return true;
		*error = QObject::tr("Could not access the system credential store. Unlock your keyring and try again.");
		return false;
	}
public:
	QString read(const QString &account, QString *error) override
	{
		QByteArray result;
		if (!run({"lookup", "service", "pastes-webdav", "account", account}, {}, &result, error)) return {};
		if (result.endsWith('\n')) result.chop(1);
		return QString::fromUtf8(result);
	}
	bool write(const QString &account, const QString &secret, QString *error) override
	{
		QByteArray result;
		return run({"store", "--label=Pastes WebDAV", "service", "pastes-webdav", "account", account}, secret.toUtf8(), &result, error);
	}
};
}
std::unique_ptr<SecretStore> Platform::createSecretStore(void)
{
	return std::make_unique<DesktopSecretStore>();
}
