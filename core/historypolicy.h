#ifndef HISTORYPOLICY_H
#define HISTORYPOLICY_H

#include <QDateTime>

namespace HistoryPolicy {

constexpr int retentionDays = 30;

inline bool expired(const QDateTime &copiedAt, const QDateTime &now)
{
	return copiedAt.secsTo(now) >= qint64(retentionDays) * 24 * 60 * 60;
}

}

#endif
