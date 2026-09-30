#include "tests/testsupport.h"
#include "platform/diagnosticlog.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

void boundedLog(void)
{
	QTemporaryDir directory;
	require(directory.isValid(), "Could not create isolated diagnostic directory");
	const QString path = directory.path()+"/logs/clipboard.jsonl";
	Platform::DiagnosticLog diagnostics(path);
	auto readLog = [&] {
		QFile file(path);
		require(file.open(QIODevice::ReadOnly), "Diagnostic log was not created");
		return file.readAll();
	};
	QJsonObject event{{"backend", "test"}, {"changeCount", 1}, {"captureAllowed", true}};
	diagnostics.append(event);
	const QByteArray first = readLog();
	const QJsonObject record = QJsonDocument::fromJson(first).object();
	require(record.value("changeCount").toInt() == 1 && record.value("captureAllowed").toBool() &&
		!record.value("time").toString().isEmpty() && record.value("processId").toInteger() > 0,
		"Diagnostic log lost supplied metadata or its timestamp");
	diagnostics.append(event);
	require(readLog() == first, "Duplicate metadata was written again");
	event.insert("captureAllowed", false);
	diagnostics.append(event);
	require(readLog().count('\n') == 2, "A policy change at the same counter was lost");

	for (int sequence = 2; sequence <= 3; ++sequence) {
		QFile full(path);
		require(full.open(QIODevice::WriteOnly) && full.resize(1024*1024), "Could not fill diagnostic log");
		full.close();
		event.insert("changeCount", sequence);
		diagnostics.append(event);
		require(QFileInfo(path+".1").size() == 1024*1024 && readLog().count('\n') == 1,
			"Diagnostic rotation did not retain only the latest file and previous generation");
	}
#ifdef Q_OS_UNIX
	const auto otherPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup |
		QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
	for (const QString &name : {path, path+".1", directory.path()+"/logs"})
		require((QFile::permissions(name) & otherPermissions) == 0, "Diagnostic files exposed metadata to other users");
#endif
	event.insert("items", QJsonArray{QString(40000, 'x')});
	diagnostics.append(event);
	const QJsonObject reduced = QJsonDocument::fromJson(readLog().trimmed().split('\n').last()).object();
	require(reduced.value("itemsOmitted").toBool() && !reduced.contains("items") &&
		reduced.value("changeCount").toInt() == 3, "Oversized diagnostic metadata lost its identity");

	QFile protectedFile(directory.path()+"/protected");
	require(protectedFile.open(QIODevice::WriteOnly) && protectedFile.write("unchanged") == 9,
		"Could not create diagnostic failure fixture");
	protectedFile.close();
	Platform::DiagnosticLog failure(protectedFile.fileName()+"/log.jsonl");
	failure.append(event);
	require(protectedFile.open(QIODevice::ReadOnly) && protectedFile.readAll() == "unchanged",
		"Diagnostic failure modified an unrelated file");
}

int main(int argc, char **argv)
{
	QCoreApplication app(argc, argv);
	return runTest("portable bounded metadata log", boundedLog);
}
