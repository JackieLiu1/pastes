#ifndef PASTES_DIAGNOSTICLOG_H
#define PASTES_DIAGNOSTICLOG_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Platform {

/* Owner-thread, bounded local metadata log. Callers supply no payloads. */
class DiagnosticLog final
{
public:
	explicit DiagnosticLog(const QString &path);
	void append(const QJsonObject &metadata);
	static QString clipboardPath(void);
	static QString inputPath(void);
	static QString focusPath(void);

private:
	QString m_path;
	QByteArray m_lastSnapshot;
};

}

#endif
