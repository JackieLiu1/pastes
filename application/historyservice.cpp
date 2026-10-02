#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "core/clipboardcontent.h"
#include "core/historypolicy.h"
#include <QCryptographicHash>
#include <utility>

namespace {
bool inheritFavorite(const HistoryEntry &entry, const HistoryEntry &previous)
{
	if (previous->favoriteModified <= entry->favoriteModified &&
		(entry->favoriteModified || !previous->favorite)) return false;
	const bool changed = entry->favorite != previous->favorite ||
		entry->favoriteModified != previous->favoriteModified;
	entry->favorite = previous->favorite;
	entry->favoriteModified = previous->favoriteModified;
	return changed;
}
}

HistoryService::HistoryService(HistoryRepository &repository, QObject *parent)
	: QObject(parent), m_repository(repository)
{
	m_undoTimer.setSingleShot(true);
	m_undoTimer.setInterval(8000);
	connect(&m_undoTimer, &QTimer::timeout, this, &HistoryService::clearUndo);
	connect(&m_repository, &HistoryRepository::loaded, this, &HistoryService::acceptLoaded);
	connect(&m_repository, &HistoryRepository::imageEncoded, this,
		[this](quint64 request, const QByteArray &encoded, int format, qreal ratio) {
		const HistoryEntry entry = find(m_imageRequests.take(request));
		if (!entry || !ClipboardData::storedImage(entry->mimeData).isEmpty()) return;
		QMimeData *mime = ClipboardData::withStoredImage(entry->mimeData, encoded, format, ratio);
		delete entry->mimeData;
		entry->mimeData = mime;
	});
}

void HistoryService::load(void)
{
	if (m_loadRequested) return;
	m_loadRequested = true;
	m_repository.load();
}

void HistoryService::acceptLoaded(const QList<HistoryEntry> &entries)
{
	if (m_ready) return;
	const QDateTime now = QDateTime::currentDateTime();
	QHash<QByteArray, HistoryEntry> storedImages;
	for (const HistoryEntry &entry : entries) {
		if (!entry || !entry->mimeData) continue;
		bool rewrite = false;
		if (!entry->favorite && HistoryPolicy::expired(entry->time, now)) {
			m_repository.remove(entry->md5);
			continue;
		}
		const QByteArray image = ClipboardContent::prefersImage(*entry->mimeData) ?
			ClipboardData::storedImage(entry->mimeData) : QByteArray();
		if (!image.isEmpty()) {
			/* Collapse identical stored originals without decoding history on
			 * startup. A later capture compares pixels only at matching sizes. */
			const QByteArray key = QCryptographicHash::hash(image, QCryptographicHash::Sha256);
			const HistoryEntry previous = storedImages.value(key);
			if (previous) {
				if (entry->time > previous->time) {
					inheritFavorite(entry, previous);
					if (entry->icon.isNull()) entry->icon = previous->icon;
					rewrite = entry->md5 == previous->md5;
					erase(indexOf(previous->id), HistoryChange::Replaced);
					if (!rewrite && (entry->favorite || entry->favoriteModified))
						m_repository.updateFavorite(entry->md5, entry->favorite, entry->favoriteModified);
				} else {
					if (inheritFavorite(previous, entry)) {
						m_repository.updateFavorite(previous->md5, previous->favorite, previous->favoriteModified);
						emit entryChanged(previous->id);
					}
					if (previous->icon.isNull() && !entry->icon.isNull()) {
						previous->icon = entry->icon;
						m_repository.updateIcon(previous->md5, previous->icon);
						emit entryChanged(previous->id);
					}
					m_repository.remove(entry->md5);
					if (entry->md5 == previous->md5) persist(previous);
					continue;
				}
			}
			storedImages.insert(key, entry);
		}
		entry->id = ++m_nextId;
		m_entries.append(entry);
		if (rewrite) persist(entry);
		emit entryAdded(entry, m_entries.size()-1, HistoryChange::Loaded);
	}
	m_ready = true;
	const auto pending = std::exchange(m_pendingCaptures, {});
	for (const auto &capture : pending) record(capture.entry, capture.sourceRequest);
	emit loaded();
}

bool HistoryService::sameContent(const HistoryEntry &left, const HistoryEntry &right)
{
	if (left->md5 == right->md5) return true;
	if (!ClipboardContent::prefersImage(*left->mimeData) ||
		!ClipboardContent::prefersImage(*right->mimeData)) return false;
	const QSize size = ClipboardData::imageSize(left->mimeData);
	if (!size.isValid() || size != ClipboardData::imageSize(right->mimeData)) return false;
	for (const auto &entry : {left, right})
		if (entry->imageContentKey.isEmpty())
			entry->imageContentKey = ClipboardContent::imageContentKey(
				qvariant_cast<QImage>(entry->mimeData->imageData()));
	return !left->imageContentKey.isEmpty() && left->imageContentKey == right->imageContentKey;
}

int HistoryService::indexOf(EntryId id) const
{
	for (int row = 0; row < m_entries.size(); ++row)
		if (m_entries.at(row)->id == id) return row;
	return -1;
}

HistoryEntry HistoryService::find(EntryId id) const
{
	const int row = indexOf(id);
	return row < 0 ? HistoryEntry() : m_entries.at(row);
}

void HistoryService::erase(int row, HistoryChange change)
{
	const HistoryEntry entry = m_entries.takeAt(row);
	emit entryErasing(entry, change);
	for (auto it = m_sourceRequests.begin(); it != m_sourceRequests.end(); )
		it = it.value() == entry->id ? m_sourceRequests.erase(it) : ++it;
	for (auto it = m_imageRequests.begin(); it != m_imageRequests.end(); )
		it = it.value() == entry->id ? m_imageRequests.erase(it) : ++it;
	m_repository.remove(entry->md5);
	emit entryRemoved(entry->id, change);
}

void HistoryService::persist(const HistoryEntry &entry)
{
	const quint64 request = m_repository.insert(entry);
	if (request) m_imageRequests.insert(request, entry->id);
}

void HistoryService::record(HistoryEntry entry, quint64 sourceRequest)
{
	if (!entry || !entry->mimeData || entry->md5.isEmpty()) return;
	if (!m_ready) {
		m_pendingCaptures.append({entry, sourceRequest});
		return;
	}
	const QDateTime now = QDateTime::currentDateTime();
	for (int row = 0; row < m_entries.size(); ) {
		const HistoryEntry previous = m_entries.at(row);
		if (sameContent(previous, entry)) {
			if (entry->icon.isNull()) entry->icon = previous->icon;
			inheritFavorite(entry, previous);
			erase(row, HistoryChange::Replaced);
		} else if (!previous->favorite && HistoryPolicy::expired(previous->time, now)) {
			erase(row, HistoryChange::Expired);
		} else ++row;
	}
	entry->id = ++m_nextId;
	if (!entry->time.isValid()) entry->time = now;
	m_entries.prepend(entry);
	if (sourceRequest) m_sourceRequests.insert(sourceRequest, entry->id);
	persist(entry);
	emit entryAdded(entry, 0, HistoryChange::Captured);
}

void HistoryService::setSourceIcon(quint64 request, const QImage &icon)
{
	if (icon.isNull()) return;
	/* A source lookup can finish while initial history is still loading. */
	for (auto &capture : m_pendingCaptures)
		if (capture.sourceRequest == request) capture.entry->icon = icon;
	const HistoryEntry entry = find(m_sourceRequests.take(request));
	if (!entry) return;
	entry->icon = icon;
	m_repository.updateIcon(entry->md5, icon);
	emit entryChanged(entry->id);
}

bool HistoryService::remove(EntryId id)
{
	const int row = indexOf(id);
	if (row < 0) return false;
	DeletedEntry removed{cloneEntry(*m_entries.at(row)), id, {}, row};
	removed.neighbors.reserve(m_entries.size());
	for (const auto &entry : m_entries)
		removed.neighbors.push_back({entry->md5, entry->time});
	if (m_deleted.size() == 20) m_deleted.erase(m_deleted.begin());
	m_deleted.push_back(std::move(removed));
	m_undoTimer.start();
	erase(row, HistoryChange::Deleted);
	emit undoChanged(true);
	return true;
}

bool HistoryService::setFavorite(EntryId id, bool favorite)
{
	const HistoryEntry entry = find(id);
	if (!entry || entry->favorite == favorite) return false;
	entry->favorite = favorite;
	entry->favoriteModified = qMax(QDateTime::currentMSecsSinceEpoch(), entry->favoriteModified+1);
	m_repository.updateFavorite(entry->md5, favorite, entry->favoriteModified);
	emit favoriteChanged(entry);
	const int row = indexOf(id);
	if (row >= 0 && !favorite && HistoryPolicy::expired(entry->time, QDateTime::currentDateTime()))
		erase(row, HistoryChange::Expired);
	return true;
}

void HistoryService::discard(EntryId id)
{
	const int row = indexOf(id);
	if (row >= 0) erase(row, HistoryChange::Discarded);
}

void HistoryService::clearUndo(void)
{
	m_undoTimer.stop();
	m_deleted.clear();
	emit undoChanged(false);
}

EntryId HistoryService::nextUndoId(void) const
{
	return m_deleted.empty() ? 0 : m_deleted.back().originalId;
}

int HistoryService::restorationRow(const DeletedEntry &removed) const
{
	const int count = m_entries.size();
	const int originalCount = static_cast<int>(removed.neighbors.size());
	int row = qBound(0, removed.row+count-(originalCount-1), count);
	QHash<QByteArray, int> neighborRows;
	for (int i = 0; i < count; ++i) neighborRows.insert(m_entries.at(i)->md5, i);
	auto originalNeighborRow = [&](int index) {
		if (index < 0 || index >= originalCount) return -1;
		const Neighbor &saved = removed.neighbors[index];
		const int currentRow = neighborRows.value(saved.md5, -1);
		return currentRow >= 0 && m_entries.at(currentRow)->time == saved.time ? currentRow : -1;
	};
	for (int distance = 1; distance < originalCount; ++distance) {
		const int previous = originalNeighborRow(removed.row-distance);
		if (previous >= 0) return previous+1;
		const int next = originalNeighborRow(removed.row+distance);
		if (next >= 0) return next;
	}
	return row;
}

HistoryService::UndoResult HistoryService::undo(void)
{
	if (m_deleted.empty()) return {};
	DeletedEntry removed = std::move(m_deleted.back());
	m_deleted.pop_back();
	UndoResult result;
	for (const auto &entry : m_entries) {
		if (sameContent(entry, removed.entry)) {
			if (removed.entry->favorite && !entry->favorite) setFavorite(entry->id, true);
			result.entry = entry;
			break;
		}
	}
	if (!result.entry) {
		const int row = restorationRow(removed);
		removed.entry->id = ++m_nextId;
		m_entries.insert(row, removed.entry);
		persist(removed.entry);
		result = {removed.entry, true};
		emit entryAdded(result.entry, row, HistoryChange::Restored);
	}
	if (!canUndo()) m_undoTimer.stop();
	emit undoChanged(canUndo());
	return result;
}

void HistoryService::mergeSynced(HistoryEntry entry, const QList<QByteArray> &replaced)
{
	if (!m_ready) return;
	for (int row = 0; row < m_entries.size(); ) {
		const auto &previous = m_entries.at(row);
		const bool duplicate = entry && sameContent(previous, entry);
		/* Metadata changes retain the newest known copy time, including legacy
		 * images whose stored identity came from a temporary URL. */
		if (duplicate && previous->time >= entry->time) {
			if (entry->favoriteModified > previous->favoriteModified) {
				previous->favorite = entry->favorite;
				previous->favoriteModified = entry->favoriteModified;
				m_repository.updateFavorite(previous->md5, previous->favorite, previous->favoriteModified);
				emit entryChanged(previous->id);
			}
			if (!previous->favorite && HistoryPolicy::expired(previous->time, QDateTime::currentDateTime())) {
				erase(row, HistoryChange::Synced);
				return;
			}
			if (!entry->icon.isNull() && previous->icon != entry->icon) {
				previous->icon = entry->icon;
				m_repository.updateIcon(previous->md5, previous->icon);
				emit entryChanged(previous->id);
			}
			return;
		}
		if (entry && duplicate && previous->favoriteModified > entry->favoriteModified) {
			entry->favorite = previous->favorite;
			entry->favoriteModified = previous->favoriteModified;
		}
		if (replaced.contains(previous->md5) || duplicate)
			erase(row, HistoryChange::Synced);
		else ++row;
	}
	if (!entry || (!entry->favorite && HistoryPolicy::expired(entry->time, QDateTime::currentDateTime()))) return;
	int row = 0;
	while (row < m_entries.size() && m_entries.at(row)->time > entry->time) ++row;
	entry->id = ++m_nextId;
	m_entries.insert(row, entry);
	persist(entry);
	emit entryAdded(entry, row, HistoryChange::Synced);
}
