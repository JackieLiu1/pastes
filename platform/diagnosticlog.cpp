#include "platform/diagnosticlog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

namespace {
constexpr qint64 maxLogBytes = 1024*1024;
constexpr qsizetype maxRecordBytes = 32*1024;
constexpr auto filePermissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

Platform::DiagnosticLog::DiagnosticLog(const QString &path) : m_path(path)
{
}

QString Platform::DiagnosticLog::clipboardPath(void)
{
	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		+"/logs/clipboard.jsonl";
}

QString Platform::DiagnosticLog::inputPath(void)
{
	return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
		+"/logs/input.jsonl";
}

void Platform::DiagnosticLog::append(const QJsonObject &metadata)
{
	QJsonObject snapshot = metadata;
	QByteArray signature = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
	if (signature.size() > maxRecordBytes) {
		/* Preserve small identity fields if a native multi-item declaration
		 * exceeds the diagnostic record budget. Never truncate JSON bytes. */
		snapshot.remove("items");
		snapshot.insert("itemsOmitted", true);
		signature = QJsonDocument(snapshot).toJson(QJsonDocument::Compact);
		if (signature.size() > maxRecordBytes) return;
	}
	if (signature == m_lastSnapshot) return;
	snapshot.insert("time", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
	snapshot.insert("processId", QCoreApplication::applicationPid());
	snapshot.insert("executable", QCoreApplication::applicationFilePath());
	const QByteArray line = QJsonDocument(snapshot).toJson(QJsonDocument::Compact)+'\n';

	const QString directory = QFileInfo(m_path).absolutePath();
	const QString previous = m_path+".1";
	if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory) ||
		!QFile::setPermissions(directory, filePermissions | QFileDevice::ExeOwner) ||
		QFileInfo(m_path).isSymLink() || QFileInfo(previous).isSymLink()) return;
	if (QFileInfo(m_path).size()+line.size() > maxLogBytes) {
		if (QFileInfo::exists(previous) && !QFile::remove(previous)) return;
		if (QFileInfo::exists(m_path) && !QFile::rename(m_path, previous)) return;
	}
	QFile file(m_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || !file.setPermissions(filePermissions)) return;
	if (file.write(line) == line.size()) m_lastSnapshot = signature;
}
