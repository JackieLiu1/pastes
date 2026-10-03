#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "core/clipboardcontent.h"
#include "core/historypolicy.h"
#include <QCryptographicHash>
#include <utility>
#include <algorithm>

namespace {
bool inheritFavorite(const HistoryEntry &entry, const HistoryEntry &previous)
{
	bool changed = mergeFavoriteDetails(entry->favoriteDetails, previous->favoriteDetails);
	if (previous->favoriteModified > entry->favoriteModified ||
		(!entry->favoriteModified && previous->favorite)) {
		changed |= entry->favorite != previous->favorite ||
			entry->favoriteModified != previous->favoriteModified;
		entry->favorite = previous->favorite;
		entry->favoriteModified = previous->favoriteModified;
	}
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
		if (!entry || !ClipboardData::storedImage(entry->mimeData.get()).isEmpty()) return;
		entry->mimeData = ClipboardData::withStoredImage(entry->mimeData.get(), encoded, format, ratio);
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
			ClipboardData::storedImage(entry->mimeData.get()) : QByteArray();
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
						persistFavorite(entry);
				} else {
					if (inheritFavorite(previous, entry)) {
						persistFavorite(previous);
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
		initializeFavoritePosition(entry);
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
	const QSize size = ClipboardData::imageSize(left->mimeData.get());
	if (!size.isValid() || size != ClipboardData::imageSize(right->mimeData.get())) return false;
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
	initializeFavoritePosition(entry);
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
	if (favorite && !entry->favoriteDetails.positionModified) {
		qint64 position = 0;
		for (const auto &other : favoriteEntries()) position = qMax(position, other->favoriteDetails.position);
		entry->favoriteDetails.position = qMin(position+1024, FavoriteDetails::maxPosition);
		entry->favoriteDetails.positionModified = entry->favoriteModified;
	}
	persistFavorite(entry);
	emit favoriteChanged(entry);
	const int row = indexOf(id);
	if (row >= 0 && !favorite && HistoryPolicy::expired(entry->time, QDateTime::currentDateTime()))
		erase(row, HistoryChange::Expired);
	return true;
}

void HistoryService::persistFavorite(const HistoryEntry &entry)
{
	m_repository.updateFavorite(entry->md5, entry->favorite, entry->favoriteModified, entry->favoriteDetails);
}

void HistoryService::initializeFavoritePosition(const HistoryEntry &entry)
{
	if (!entry->favorite || entry->favoriteDetails.positionModified) return;
	/* Use the existing operation's clock on every device, so migration is
	 * deterministic and cannot replace a later manual ordering operation. */
	const qint64 clock = qMax(qint64(1), entry->favoriteModified ? entry->favoriteModified : entry->time.toMSecsSinceEpoch());
	entry->favoriteDetails.position = qMin(clock, FavoriteDetails::maxPosition);
	entry->favoriteDetails.positionModified = clock;
	persistFavorite(entry);
}

QList<HistoryEntry> HistoryService::favoriteEntries(void) const
{
	QList<HistoryEntry> result;
	for (const auto &entry : m_entries) if (entry->favorite) result.append(entry);
	std::sort(result.begin(), result.end(), [](const HistoryEntry &a, const HistoryEntry &b) {
		return a->favoriteDetails.position != b->favoriteDetails.position ?
			a->favoriteDetails.position < b->favoriteDetails.position : a->md5 < b->md5;
	});
	return result;
}

bool HistoryService::setFavoriteName(EntryId id, const QString &name)
{
	const auto entry = find(id);
	QString value = name.simplified();
	if (value.size() > FavoriteDetails::maxNameLength) {
		value.truncate(FavoriteDetails::maxNameLength);
		if (value.back().isHighSurrogate()) value.chop(1);
	}
	if (!entry || !entry->favorite || value == entry->favoriteDetails.name) return false;
	entry->favoriteDetails.name = value;
	entry->favoriteDetails.nameModified = qMax(QDateTime::currentMSecsSinceEpoch(), entry->favoriteDetails.nameModified+1);
	persistFavorite(entry);
	emit favoriteChanged(entry);
	return true;
}

bool HistoryService::moveFavorite(EntryId id, EntryId neighbor)
{
	const auto entry = find(id), other = find(neighbor);
	if (!entry || !other || entry == other || !entry->favorite || !other->favorite) return false;
	QList<HistoryEntry> changed;
	const auto favorites = favoriteEntries();
	/* Equal ranks can come from concurrent inserts on separate devices. */
	if (entry->favoriteDetails.position == other->favoriteDetails.position) {
		for (int i = 0; i < favorites.size(); ++i) {
			favorites[i]->favoriteDetails.position = qint64(i+1)*1024;
			changed.append(favorites[i]);
		}
	}
	std::swap(entry->favoriteDetails.position, other->favoriteDetails.position);
	if (!changed.contains(entry)) changed.append(entry);
	if (!changed.contains(other)) changed.append(other);
	for (const auto &item : changed) {
		item->favoriteDetails.positionModified = qMax(QDateTime::currentMSecsSinceEpoch(), item->favoriteDetails.positionModified+1);
		persistFavorite(item);
	}
	/* Publish after all values change; observers see a complete swap. */
	for (const auto &item : changed) emit favoriteChanged(item);
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
			const bool changed = mergeFavoriteDetails(entry->favoriteDetails, removed.entry->favoriteDetails);
			if (removed.entry->favorite && !entry->favorite) setFavorite(entry->id, true);
			else if (changed) { persistFavorite(entry); emit favoriteChanged(entry); }
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
			if (inheritFavorite(previous, entry)) {
				initializeFavoritePosition(previous);
				persistFavorite(previous);
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
		if (entry && duplicate) inheritFavorite(entry, previous);
		if (replaced.contains(previous->md5) || duplicate)
			erase(row, HistoryChange::Synced);
		else ++row;
	}
	if (!entry || (!entry->favorite && HistoryPolicy::expired(entry->time, QDateTime::currentDateTime()))) return;
	int row = 0;
	while (row < m_entries.size() && m_entries.at(row)->time > entry->time) ++row;
	entry->id = ++m_nextId;
	initializeFavoritePosition(entry);
	m_entries.insert(row, entry);
	persist(entry);
	emit entryAdded(entry, row, HistoryChange::Synced);
}
