#ifndef PASTES_DAV_FIXTURE_H
#define PASTES_DAV_FIXTURE_H
#include "tests/support/testsupport.h"
#include "sync/webdavsync.h"
#include "core/sourceicon.h"
#include "platform/secretstore.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QJsonDocument>
namespace
{
class DavServer final : public QTcpServer
{
public:
	DavServer()
	{
		require(listen(QHostAddress::LocalHost), "Cannot listen on local WebDAV test port");
		connect(this, &QTcpServer::newConnection, this, [this] {
			while (hasPendingConnections()) {
				auto *socket = nextPendingConnection();
				auto bytes = std::make_shared<QByteArray>();
				connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
				connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
					bytes->append(socket->readAll());
					const int end = bytes->indexOf("\r\n\r\n");
					if (end < 0) return;
					const auto headers = bytes->left(end).split('\n');
					const auto request = headers.first().trimmed().split(' ');
					QHash<QByteArray, QByteArray> fields;
					for (const auto &line : headers) {
						const int colon = line.indexOf(':');
						if (colon > 0)
							fields[line.left(colon).trimmed().toLower()] =
								line.mid(colon + 1).trimmed();
					}
					const int length = fields.value("content-length").toInt();
					if (bytes->size() < end + 4 + length) return;
					const QByteArray body = bytes->mid(end + 4, length);
					const QByteArray method = request.value(0);
					if (onRequest) onRequest(method);
					const QString path = QString::fromUtf8(request.value(1));
					int status = 200;
					QByteArray response;
					if (fields.value("authorization") != "Basic dXNlcjpwYXNz")
						status = 401;
					else if (redirect)
						status = 302;
					else if (method == "MKCOL")
						status = 201;
					else if (method == "PROPFIND") {
						++listings;
						status = 207;
						response = "<d:multistatus xmlns:d=\"DAV:\">";
						for (auto it = files.begin(); it != files.end(); ++it)
							response += "<d:response><d:href>" + it.key().toUtf8() +
								"</d:href></d:response>";
						response += "</d:multistatus>";
						if (badListing) response = "<html>login required</html>";
					} else if (method == "GET") {
						++gets;
						if (!files.contains(path))
							status = 404;
						else
							response = corrupt ? QByteArray("{}") : files.value(path);
					} else if (method == "PUT") {
						++puts;
						if (failPut) {
							failPut = false;
							status = 503;
						} else if (files.contains(path) && fields.value("if-none-match") == "*")
							status = 412;
						else {
							files[path] = body;
							status = 201;
						}
					} else if (method == "DELETE") {
						status = files.remove(path) ? 204 : 404;
					} else
						status = 405;
					socket->write("HTTP/1.1 " + QByteArray::number(status) +
						" Result\r\nConnection: close\r\nContent-Length: " +
						QByteArray::number(response.size()) + "\r\n\r\n" + response);
					socket->disconnectFromHost();
				});
			}
		});
	}
	SyncSettings settings() const
	{
		return {QUrl(QString("http://127.0.0.1:%1/").arg(serverPort())), "user", true};
	}
	QHash<QString, QByteArray> files;
	int puts = 0, gets = 0, listings = 0;
	std::function<void(const QByteArray &)> onRequest;
	bool failPut = false, corrupt = false, redirect = false, badListing = false;
};
class Client final : public QObject
{
public:
	Client(const SyncSettings &settings, const QString &directory, const QString &password = "pass")
	{
		worker = new WebDavWorker;
		worker->moveToThread(&thread);
		connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
		connect(worker, &WebDavWorker::delivered, this, [this](const SyncDelivery &value) {
			deliveries.append(value);
			clock.observe(value.events.latest());
		});
		connect(
			worker, &WebDavWorker::finished, this, [this](const QString &message, bool ok, bool) {
				++finished;
				success = ok;
				error = message;
			});
		thread.start();
		QMetaObject::invokeMethod(
			worker, [=] { worker->configure(settings, password, directory, false); });
	}
	~Client()
	{
		QMetaObject::invokeMethod(worker, &WebDavWorker::stop, Qt::BlockingQueuedConnection);
		thread.quit();
		thread.wait();
	}
	void capture(const HistoryEntry &entry, bool bootstrap = false, bool deleted = false)
	{
		Q_UNUSED(bootstrap);
		auto value = SyncContent::snapshot(*entry);
		if (!value.events.copy.valid())
			value.events.copy = {value.time.toMSecsSinceEpoch(), 0, "test-source"};
		if (!value.events.source.valid() && SourceIcon::captured(value.icon)) {
			value.events.source = {SourceIcon::captured(value.icon), 0, "test-source"};
			value.events.sourceCaptured = SourceIcon::captured(value.icon);
		}
		clock.observe(value.events.latest());
		if (deleted) {
			value.events.erase = clock.tick();
			value.events.erasedAt = QDateTime::currentMSecsSinceEpoch();
			value.events.favorite = value.events.name = value.events.position = value.events.erase;
			value.favorite = false;
			value.favoriteDetails = {};
		}
		QString error;
		const auto document = SyncContent::document(value, deleted, {}, &error);
		require(error.isEmpty(), qPrintable(error));
		QMetaObject::invokeMethod(
			worker, [worker = worker, document] { worker->accept(document); });
	}

	void run(bool probe = false)
	{
		const int previous = finished;
		deliveries.clear();
		QMetaObject::invokeMethod(worker, [=] { worker->run(probe); });
		waitUntil([&] { return finished > previous; }, 10000);
	}
	QThread thread;
	WebDavWorker *worker;
	QList<SyncDelivery> deliveries;
	EventClock clock;
	int finished = 0;
	bool success = false;
	QString error;
};

class MemorySecrets final : public SecretStore
{
public:
	QString read(const QString &account, QString *) override { return values.value(account); }
	bool write(const QString &account, const QString &secret, QString *) override
	{
		values[account] = secret;
		return true;
	}
	QHash<QString, QString> values;
};

} // namespace
#endif
