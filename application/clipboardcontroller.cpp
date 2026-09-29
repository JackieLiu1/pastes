#include "application/clipboardcontroller.h"
#include "core/clipboardcontent.h"
#include "core/clipboarddata.h"

#include <QClipboard>
#include <QVariant>

namespace {
constexpr char sourceIconProperty[] = "pastesSourceIcon";
}

ClipboardController::ClipboardController(HistoryService &history, ClipboardFeed &feed,
	QClipboard &clipboard, bool recordingEnabled, QObject *parent)
	: QObject(parent), m_history(history), m_feed(feed), m_clipboard(clipboard),
	  m_recordingEnabled(recordingEnabled)
{
	m_timer.setSingleShot(true);
	m_timer.setInterval(m_feed.settleInterval());
	connect(&m_timer, &QTimer::timeout, this, &ClipboardController::capture);
	connect(&m_feed, &ClipboardFeed::clipboardChanged, this, &ClipboardController::clipboardChanged);
	connect(&m_feed, &ClipboardFeed::iconReady, this,
		[this](quint64 request, const QImage &icon) {
		if (request == m_sourceRequest) m_sourceIcon = icon;
		m_history.setSourceIcon(request, icon);
	}, Qt::QueuedConnection);
}

void ClipboardController::clipboardChanged(void)
{
	m_timer.stop();
	++m_sourceRequest;
	m_sourceIcon = QImage();
	if (!m_recordingEnabled || !m_feed.allowsCapture()) return;
	const QMimeData *mime = m_clipboard.mimeData();
	if (!mime) return;
	const QVariant icon = mime->property(sourceIconProperty);
	m_sourceIcon = icon.value<QImage>();
	if (!icon.isValid()) m_feed.capture(m_sourceRequest);
	m_timer.start();
}

void ClipboardController::capture(void)
{
	if (!m_recordingEnabled || m_feed.synchronize() || !m_feed.allowsCapture()) return;
	const QMimeData *mime = m_clipboard.mimeData();
	if (!mime) return;
	const quint64 request = m_sourceRequest;
	const QImage sourceIcon = m_sourceIcon;
	const QVariant copiedIcon = mime->property(sourceIconProperty);
	auto entry = HistoryEntry::create();
	entry->mimeData = ClipboardData::duplicate(mime);
	/* Reading promised MIME flavors may cause a newer native notification. */
	if (m_feed.synchronize() || !m_feed.allowsCapture()) return;
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	if (entry->md5.isEmpty()) return;
	entry->icon = copiedIcon.isValid() ? copiedIcon.value<QImage>() :
		(sourceIcon.isNull() ? m_feed.snapshotIcon() : sourceIcon);
	entry->time = QDateTime::currentDateTime();
	m_history.record(entry, copiedIcon.isValid() ? 0 : request);
}

bool ClipboardController::copy(const ItemData &entry, bool plainText)
{
	if (!entry.mimeData || (plainText && !entry.mimeData->hasText())) return false;
	QMimeData *mime = plainText ? new QMimeData : ClipboardData::duplicate(entry.mimeData);
	if (plainText) mime->setText(entry.mimeData->text());
	/* Preserve source identity without exporting an extra MIME format. */
	mime->setProperty(sourceIconProperty, entry.icon);
	m_clipboard.setMimeData(mime, QClipboard::Clipboard);
	if (m_clipboard.supportsSelection())
		m_clipboard.setMimeData(ClipboardData::duplicate(mime), QClipboard::Selection);
	/* Also cover internal writes whose native notification was synchronous. */
	if (m_recordingEnabled) m_timer.start();
	return true;
}

void ClipboardController::setRecordingEnabled(bool enabled)
{
	/* Consume pending native copies under the previous recording state. */
	m_feed.synchronize();
	m_recordingEnabled = enabled;
	m_timer.stop();
	++m_sourceRequest;
	m_sourceIcon = QImage();
	emit recordingChanged(enabled);
}

void ClipboardController::flushPending(void)
{
	m_feed.synchronize();
	if (m_feed.settleInterval() == 0 && m_timer.isActive()) {
		m_timer.stop();
		capture();
	}
}
