#include "application/clipboardcontroller.h"
#include "core/clipboardcontent.h"
#include "core/clipboarddata.h"
#include "core/sourceicon.h"

#include <QClipboard>
#include <QVariant>

namespace {
constexpr char sourceIconProperty[] = "pastesSourceIcon";
constexpr char sourceEventsProperty[] = "pastesSourceEvents";
constexpr char copyVersionProperty[] = "pastesCopyVersion";
constexpr char copyTimeProperty[] = "pastesCopyTime";
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
	connect(
		&m_feed, &ClipboardFeed::iconReady, this,
		[this](quint64 request, const QImage &icon) {
			if (request == m_sourceRequest) {
				m_sourceIcon = icon;
				m_sourceComplete = true;
			}
			m_history.setSourceIcon(request, icon);
		},
		Qt::QueuedConnection);
}

void ClipboardController::clipboardChanged(void)
{
	m_timer.stop();
	++m_sourceRequest;
	m_captureTime = QDateTime::currentDateTime();
	m_copyVersion = {};
	m_sourceIcon = QImage();
	m_sourceComplete = false;
	if (!m_recordingEnabled || !m_feed.allowsCapture()) return;
	const QMimeData *mime = m_clipboard.mimeData();
	if (!mime) return;
	const QVariant icon = mime->property(sourceIconProperty);
	if (icon.isValid()) {
		const auto version =
			EventVersion::parse(mime->property(copyVersionProperty).toJsonObject());
		const auto time = mime->property(copyTimeProperty).toDateTime();
		if (version.valid()) m_copyVersion = version;
		if (time.isValid()) m_captureTime = time;
	}
	if (!m_copyVersion.valid()) m_copyVersion = m_history.beginCopy();
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
	const auto inherited = EventState::parse(mime->property(sourceEventsProperty).toJsonObject());
	auto entry = HistoryEntry::create();
	entry->mimeData = ClipboardData::duplicate(mime, m_feed.hasImageContent());
	/* Reading promised MIME flavors may cause a newer native notification. */
	if (m_feed.synchronize() || request != m_sourceRequest || !m_feed.allowsCapture()) return;
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	if (entry->md5.isEmpty()) return;
	entry->time = m_captureTime.isValid() ? m_captureTime : QDateTime::currentDateTime();
	entry->events.copy = m_copyVersion;
	entry->sourceInherited = copiedIcon.isValid();
	entry->events.source = inherited.source;
	entry->events.sourceCaptured = inherited.sourceCaptured;
	entry->events.sourceRevision =
		copiedIcon.isValid() ? inherited.sourceRevision : int(m_sourceComplete);
	entry->icon = copiedIcon.isValid()
		? copiedIcon.value<QImage>()
		: SourceIcon::observed(sourceIcon.isNull() ? m_feed.snapshotIcon() : sourceIcon,
			  entry->time.toMSecsSinceEpoch());
	m_history.record(entry, copiedIcon.isValid() ? 0 : request);
}

bool ClipboardController::copy(const ItemData &entry, bool plainText)
{
	if (!entry.mimeData || (plainText && !entry.mimeData->hasText())) return false;
	auto mime = plainText ? std::make_unique<QMimeData>() :
		ClipboardData::duplicate(entry.mimeData.get());
	if (plainText) mime->setText(entry.mimeData->text());
	/* Preserve source identity without exporting an extra MIME format. */
	mime->setProperty(sourceIconProperty, entry.icon);
	mime->setProperty(sourceEventsProperty, entry.events.json());
	m_captureTime = QDateTime::currentDateTime();
	m_copyVersion = m_history.beginCopy();
	++m_sourceRequest;
	m_sourceIcon = entry.icon;
	m_sourceComplete = true;
	mime->setProperty(copyVersionProperty, m_copyVersion.json());
	mime->setProperty(copyTimeProperty, m_captureTime);
	/* A synchronous clipboard notification may replace and destroy its data.
	 * Prepare both snapshots before transferring either one to Qt. */
	auto selection = m_clipboard.supportsSelection() ? ClipboardData::duplicate(mime.get()) : nullptr;
	m_clipboard.setMimeData(mime.release(), QClipboard::Clipboard);
	if (selection) m_clipboard.setMimeData(selection.release(), QClipboard::Selection);
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
