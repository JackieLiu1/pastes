#ifndef PASTES_CLIPBOARDCONTROLLER_H
#define PASTES_CLIPBOARDCONTROLLER_H

#include "application/clipboardfeed.h"
#include "application/historyservice.h"
#include <QTimer>

class QClipboard;

/* Clipboard input/output and native notification settling. This controller
 * knows neither widgets nor SQL; all captures enter through HistoryService. */
class ClipboardController final : public QObject
{
	Q_OBJECT
public:
	ClipboardController(HistoryService &history, ClipboardFeed &feed,
		QClipboard &clipboard, bool recordingEnabled, QObject *parent = nullptr);
	bool copy(const ItemData &entry, bool plainText = false);
	bool recordingEnabled(void) const { return m_recordingEnabled; }
	void setRecordingEnabled(bool enabled);
	void flushPending(void);

signals:
	void recordingChanged(bool enabled);

private:
	void clipboardChanged(void);
	void capture(void);
	HistoryService &m_history;
	ClipboardFeed &m_feed;
	QClipboard &m_clipboard;
	QTimer m_timer;
	quint64 m_sourceRequest = 0;
	QImage m_sourceIcon;
	bool m_recordingEnabled;
};

#endif
