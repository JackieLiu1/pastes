#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "core/clipboardcontent.h"
#include "core/historypolicy.h"
#include "core/sourceicon.h"
#include "core/synccontent.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <utility>
#include <algorithm>

namespace {
void normalize(const HistoryEntry &entry)
{
	if (entry->favorite && !entry->favoriteModified)
		entry->favoriteModified = qMax(qint64(1), entry->time.toMSecsSinceEpoch());
	if (!entry->events.copy.valid())
		entry->events.copy = EventVersion::legacy(entry->time.toMSecsSinceEpoch());
	if (!entry->events.favorite.valid())
		entry->events.favorite = EventVersion::legacy(entry->favoriteModified);
	if (!entry->events.name.valid())
		entry->events.name = EventVersion::legacy(entry->favoriteDetails.nameModified);
	if (!entry->events.position.valid())
		entry->events.position = EventVersion::legacy(entry->favoriteDetails.positionModified);
}
bool inheritFavorite(const HistoryEntry &entry, const HistoryEntry &previous)
{
	const auto old = SyncContent::snapshot(*entry);
	const auto merged = SyncContent::merge(old, SyncContent::snapshot(*previous));
	entry->icon = merged.icon;
	entry->events = merged.events;
	entry->favorite = merged.favorite;
	entry->favoriteModified = merged.favoriteModified;
	entry->favoriteDetails = merged.favoriteDetails;
	return old.favorite != merged.favorite || old.favoriteModified != merged.favoriteModified ||
		old.favoriteDetails != merged.favoriteDetails || old.icon != merged.icon ||
		old.events.latest() != merged.events.latest();
}
}

HistoryService::HistoryService(HistoryRepository &repository, QObject *parent)
	: QObject(parent), m_repository(repository)
{
	m_undoTimer.setSingleShot(true);
	m_undoTimer.setInterval(8000);
	connect(&m_undoTimer, &QTimer::timeout, this, &HistoryService::clearUndo);
	connect(&m_repository, &HistoryRepository::clockLoaded, this,
		[this](const QString &device, const EventVersion &version) {
			m_clock.device = device;
			m_clock.observe(version);
		});
	connect(&m_repository, &HistoryRepository::projected, this,
		[this](quint64 request, const HistoryEntry &entry, bool deleted) {
			if (!m_writes.contains(request)) return;
			if (!entry->contentKey.isEmpty()) m_contentKeys[entry->md5] = entry->contentKey;
			const auto &pending = m_writes[request];
			auto replaced = pending.removed;
			if (!pending.entry->md5.isEmpty()) replaced.append(pending.entry->md5);
			mergeIncoming(deleted ? HistoryEntry() : entry, replaced, entry->events,
				entry->contentKey, {}, {}, SyncContent::snapshot(*entry), false);
		});
	connect(&m_repository, &HistoryRepository::committed, this,
		[this](quint64 request, const QByteArray &document) {
			if (!m_writes.contains(request)) return;
			const auto pending = m_writes.take(request);
			m_imageRequests.remove(request);
			if (!document.isEmpty()) {
				const auto object = QJsonDocument::fromJson(document).object();
				m_clock.observe(EventState::parse(object.value("events").toObject()).latest());
				if (!pending.entry->md5.isEmpty()) {
					m_contentKeys[pending.entry->md5] = object.value("key").toString();
					const auto current = find(pending.entry->id);
					if (current && current->events.copy == pending.entry->events.copy)
						current->contentKey = object.value("key").toString();
				}
			}
			emit durableChanged(document);
			if (!pending.record.isEmpty()) emit syncApplied(pending.account, pending.record, true);
			drainIncoming();
			emit persistenceStateChanged();
		});
	connect(&m_repository, &HistoryRepository::commitFailed, this,
		[this](quint64 request, const QString &error) {
			if (!m_writes.contains(request)) return;
			auto &pending = m_writes[request];
			pending.failed = true;
			m_imageRequests.remove(request);
			emit persistenceFailed(error);
			if (!pending.record.isEmpty()) emit syncApplied(pending.account, pending.record, false);
			drainIncoming();
			emit persistenceStateChanged();
		});
	connect(&m_repository, &HistoryRepository::appliedLoaded, this, &HistoryService::appliedLoaded);
	connect(&m_repository, &HistoryRepository::outboxLoaded, this, &HistoryService::outboxLoaded);
	connect(&m_repository, &HistoryRepository::published, this, &HistoryService::published);
	connect(&m_repository, &HistoryRepository::publishFailed, this, &HistoryService::publishFailed);
	connect(&m_repository, &HistoryRepository::syncWarning, this, &HistoryService::syncWarning);
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
		normalize(entry);
		m_clock.observe(entry->events.latest());
		if (!entry->contentKey.isEmpty()) m_contentKeys[entry->md5] = entry->contentKey;
		QList<QByteArray> rewrite;
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
				if (entry->events.copy > previous->events.copy) {
					inheritFavorite(entry, previous);
					rewrite = {previous->md5, entry->md5};
					erase(indexOf(previous->id), HistoryChange::Replaced, false);
				} else {
					const bool changed = inheritFavorite(previous, entry);
					persistHousekeeping(previous, {previous->md5, entry->md5});
					if (changed) emit entryChanged(previous->id);
					continue;
				}
			}
			storedImages.insert(key, entry);
		}
		entry->id = ++m_nextId;
		initializeFavoritePosition(entry);
		m_entries.append(entry);
		if (!rewrite.isEmpty()) persistHousekeeping(entry, rewrite);
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
	QImage images[2];
	const HistoryEntry entries[] = {left, right};
	for (int i = 0; i < 2; ++i)
		if (entries[i]->imageContentKey.isEmpty()) {
			images[i] = qvariant_cast<QImage>(entries[i]->mimeData->imageData());
			entries[i]->imageContentKey = ClipboardContent::imageContentKey(images[i]);
			entries[i]->imageColorSpace = images[i].colorSpace();
		}
	if (!left->imageContentKey.isEmpty() && left->imageContentKey == right->imageContentKey)
		return true;
	/* Most same-size screenshots share a profile. Cached metadata avoids
	 * decoding them again when their exact pixel keys already differ. */
	if (!left->imageColorSpace.isValid() || !right->imageColorSpace.isValid() ||
		left->imageColorSpace.iccProfile() == right->imageColorSpace.iccProfile()) return false;
	for (int i = 0; i < 2; ++i)
		if (images[i].isNull()) images[i] = qvariant_cast<QImage>(entries[i]->mimeData->imageData());
	if (!ClipboardContent::sameImageColors(images[0], images[1])) return false;
	for (int i = 0; i < 2; ++i) {
		if (!entries[i]->imageSyncKey.isEmpty()) continue;
		SyncContent::Snapshot value;
		value.kind = "image";
		value.image = images[i];
		entries[i]->imageSyncKey = SyncContent::key(value);
	}
	return true;
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

void HistoryService::erase(int row, HistoryChange change, bool write)
{
	const HistoryEntry entry = m_entries.takeAt(row);
	emit entryErasing(entry, change);
	for (auto it = m_sourceRequests.begin(); it != m_sourceRequests.end(); )
		it = it.value().entry == entry->id ? m_sourceRequests.erase(it) : ++it;
	for (auto it = m_imageRequests.begin(); it != m_imageRequests.end(); )
		it = it.value() == entry->id ? m_imageRequests.erase(it) : ++it;
	if (write) {
		if (change == HistoryChange::Deleted)
			persist(entry, {entry->md5}, true);
		else
			m_repository.remove(entry->md5);
	}
	emit entryRemoved(entry->id, change);
}

void HistoryService::persist(const HistoryEntry &entry, const QList<QByteArray> &removed,
	bool deleted, const QString &key, const QString &account, const QString &record, bool publish,
	const QSet<QString> &retiredImageKeys)
{
	const auto snapshot = cloneEntry(*entry);
	snapshot->id = entry->id;
	const quint64 request =
		m_repository.save(snapshot, {removed, deleted, publish, key, account, record, retiredImageKeys});
	m_writes.insert(request,
		{snapshot, removed, deleted, key, account, record, false, publish, retiredImageKeys});
	emit persistenceStateChanged();
	if (request && !deleted) m_imageRequests.insert(request, entry->id);
}
void HistoryService::retryPersistence(void)
{
	const auto pending = m_writes;
	for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
		if (!it->failed) continue;
		m_writes.remove(it.key());
		auto value = it.value();
		for (const auto &current : m_entries)
			if (current->md5 == value.entry->md5) {
				if (current->events.presence() > value.entry->events.erase) {
					value.entry = cloneEntry(*current);
					value.entry->id = current->id;
					value.deleted = false;
				}
			}
		persist(value.entry, value.removed, value.deleted, value.key, value.account, value.record,
			value.publish, value.retiredImageKeys);
	}
}

void HistoryService::record(HistoryEntry entry, quint64 sourceRequest)
{
	if (!entry || !entry->mimeData || entry->md5.isEmpty()) return;
	if (!m_ready) {
		m_pendingCaptures.append({entry, sourceRequest});
		return;
	}
	const QDateTime now = QDateTime::currentDateTime();
	if (!entry->time.isValid()) entry->time = now;
	if (!entry->events.copy.valid()) entry->events.copy = m_clock.tick();
	if (!entry->icon.isNull() && !entry->events.source.valid() && !entry->sourceInherited) {
		entry->events.source = entry->events.copy;
		entry->events.sourceCaptured = entry->time.toMSecsSinceEpoch();
	}
	m_clock.observe(entry->events.latest());
	QList<QByteArray> replaced;
	QSet<QString> retiredImageKeys;
	for (int row = 0; row < m_entries.size(); ) {
		const HistoryEntry previous = m_entries.at(row);
		if (sameContent(previous, entry)) {
			if (previous->events.copy >= entry->events.copy) {
				if (sourceRequest)
					m_sourceRequests.insert(sourceRequest,
						{previous->id, entry->time.toMSecsSinceEpoch(), entry->events.copy});
				QSet<QString> retired;
				if (!entry->imageSyncKey.isEmpty() && entry->imageSyncKey != previous->imageSyncKey)
					retired.insert(entry->imageSyncKey);
				const bool changed = inheritFavorite(previous, entry);
				if (changed || !retired.isEmpty())
					persist(previous, {previous->md5}, false, {}, {}, {}, true, retired);
				if (changed) emit entryChanged(previous->id);
				return;
			}
			inheritFavorite(entry, previous);
			if (!previous->imageSyncKey.isEmpty() && previous->imageSyncKey != entry->imageSyncKey)
				retiredImageKeys.insert(previous->imageSyncKey);
			entry->contentKey = previous->contentKey;
			replaced.append(previous->md5);
			erase(row, HistoryChange::Replaced, false);
		} else if (!previous->favorite && HistoryPolicy::expired(previous->time, now)) {
			erase(row, HistoryChange::Expired);
		} else ++row;
	}
	entry->id = ++m_nextId;
	if (!entry->time.isValid()) entry->time = now;
	initializeFavoritePosition(entry);
	m_entries.prepend(entry);
	if (sourceRequest)
		m_sourceRequests.insert(
			sourceRequest, {entry->id, entry->time.toMSecsSinceEpoch(), entry->events.copy});
	persist(entry, replaced, false, {}, {}, {}, true, retiredImageKeys);
	emit entryAdded(entry, 0, HistoryChange::Captured);
}

void HistoryService::setSourceIcon(quint64 request, const QImage &icon)
{
	if (icon.isNull()) return;
	for (auto &capture : m_pendingCaptures)
		if (capture.sourceRequest == request) {
			capture.entry->icon = icon;
			capture.entry->events.sourceRevision = 1;
		}
	const auto source = m_sourceRequests.take(request);
	const auto entry = find(source.entry);
	if (!entry || source.version < entry->events.source) return;
	if (source.version == entry->events.source && entry->events.sourceRevision &&
		!entry->icon.isNull() && SourceIcon::key(icon) <= SourceIcon::key(entry->icon))
		return;
	entry->icon = SourceIcon::observed(icon, source.captured);
	entry->events.source = source.version;
	entry->events.sourceCaptured = source.captured;
	entry->events.sourceRevision = 1;
	persist(entry, {entry->md5});
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
	m_entries.at(row)->events.erase = m_clock.tick();
	m_entries.at(row)->events.erasedAt = QDateTime::currentMSecsSinceEpoch();
	m_entries.at(row)->events.favorite = m_entries.at(row)->events.name =
		m_entries.at(row)->events.position = m_entries.at(row)->events.erase;
	m_entries.at(row)->favorite = false;
	m_entries.at(row)->favoriteDetails = {};
	erase(row, HistoryChange::Deleted);
	emit undoChanged(true);
	return true;
}

bool HistoryService::setFavorite(EntryId id, bool favorite)
{
	const HistoryEntry entry = find(id);
	if (!entry || entry->favorite == favorite) return false;
	entry->events.favorite = m_clock.tick();
	entry->favorite = favorite;
	entry->favoriteModified = entry->events.favorite.physical;
	if (favorite && !entry->favoriteDetails.positionModified) {
		qint64 position = 0;
		for (const auto &other : favoriteEntries()) position = qMax(position, other->favoriteDetails.position);
		entry->favoriteDetails.position = qMin(position+1024, FavoriteDetails::maxPosition);
		entry->events.position = m_clock.tick();
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
	persist(entry, {entry->md5});
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
	entry->events.name = m_clock.tick();
	entry->favoriteDetails.name = value;
	entry->favoriteDetails.nameModified = entry->events.name.physical;
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
		item->events.position = m_clock.tick();
		item->favoriteDetails.positionModified = item->events.position.physical;
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
			const auto previousDetails = entry->favoriteDetails;
			const bool changed = mergeFavoriteDetails(entry->favoriteDetails, removed.entry->favoriteDetails);
			if (entry->favoriteDetails.name != previousDetails.name)
				entry->events.name = m_clock.tick();
			if (entry->favoriteDetails.position != previousDetails.position)
				entry->events.position = m_clock.tick();
			if (removed.entry->favorite && !entry->favorite) setFavorite(entry->id, true);
			else if (changed) { persistFavorite(entry); emit favoriteChanged(entry); }
			result.entry = entry;
			break;
		}
	}
	if (!result.entry) {
		const int row = restorationRow(removed);
		removed.entry->events.copy = m_clock.tick();
		removed.entry->events.favorite = removed.entry->events.name =
			removed.entry->events.position = removed.entry->events.copy;
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
	EventState events;
	if (entry) {
		normalize(entry);
		events = entry->events;
	}
	mergeIncoming(entry, replaced, events, {}, {}, {});
}
void HistoryService::applySynced(HistoryEntry entry, const QList<QByteArray> &replaced,
	const EventState &events, const QString &key, const QString &account, const QString &record,
	const SyncContent::Snapshot &metadata)
{
	m_clock.observe(events.latest());
	Incoming incoming{entry, replaced, events, key, account, record, metadata, {}};
	for (auto it = m_writes.cbegin(); it != m_writes.cend(); ++it)
		incoming.barrier.insert(it.key());
	m_incoming.append(incoming);
	drainIncoming();
}
void HistoryService::drainIncoming(void)
{
	for (int i = 0; i < m_incoming.size();) {
		bool waiting = false, failed = false;
		for (auto request : m_incoming[i].barrier)
			if (m_writes.contains(request)) {
				if (m_writes[request].failed)
					failed = true;
				else
					waiting = true;
			}
		if (waiting) {
			++i;
			continue;
		}
		const auto incoming = m_incoming.takeAt(i);
		if (failed) {
			emit syncApplied(incoming.account, incoming.record, false);
			continue;
		}
		mergeIncoming(incoming.entry, incoming.replaced, incoming.events, incoming.key,
			incoming.account, incoming.record, incoming.metadata);
	}
}
void HistoryService::mergeIncoming(HistoryEntry entry, const QList<QByteArray> &replaced,
	const EventState &events, const QString &key, const QString &account, const QString &record,
	const SyncContent::Snapshot &metadata, bool persistResult)
{
	if (!m_ready) return;
	if (entry && !key.isEmpty()) entry->contentKey = key;
	m_clock.observe(events.latest());
	const QDateTime now = QDateTime::currentDateTime();
	EntryId replacementId = 0;
	QList<QByteArray> removed;
	QSet<QString> retiredImageKeys;
	for (int row = 0; row < m_entries.size();) {
		const auto previous = m_entries.at(row);
		const bool duplicate = entry && sameContent(previous, entry);
		if (!duplicate && !replaced.contains(previous->md5) &&
			(key.isEmpty() || m_contentKeys.value(previous->md5) != key)) {
			++row;
			continue;
		}
		if (!entry && events.erase.valid() && previous->events.presence() > events.erase) {
			auto tombstone = SyncContent::materialize(metadata);
			tombstone->events = events;
			if (inheritFavorite(previous, tombstone)) emit entryChanged(previous->id);
			if (persistResult) persist(previous, {previous->md5}, false, key, account, record);
			return;
		}
		if (duplicate && previous->events.copy >= entry->events.copy) {
			const bool changed = inheritFavorite(previous, entry);
			if (!entry->imageSyncKey.isEmpty() && entry->imageSyncKey != previous->imageSyncKey)
				retiredImageKeys.insert(entry->imageSyncKey);
			if (persistResult)
				persist(previous, {previous->md5}, false, key, account, record, true, retiredImageKeys);
			if (changed) emit entryChanged(previous->id);
			return;
		}
		if (entry) {
			inheritFavorite(entry, previous);
			if (duplicate && !previous->imageSyncKey.isEmpty() && previous->imageSyncKey != entry->imageSyncKey)
				retiredImageKeys.insert(previous->imageSyncKey);
		}
		if (entry && (entry->favorite || !HistoryPolicy::expired(entry->time, now))) {
			if (!replacementId) replacementId = ++m_nextId;
			for (auto &source : m_sourceRequests)
				if (source.entry == previous->id) source.entry = replacementId;
			emit entryReplacing(previous->id, replacementId);
		}
		removed.append(previous->md5);
		erase(row, HistoryChange::Synced, false);
	}
	if (!entry || (!entry->favorite && HistoryPolicy::expired(entry->time, now))) {
		if (!record.isEmpty()) {
			auto tombstone = SyncContent::materialize(metadata);
			tombstone->events = events;
			if (persistResult) persist(tombstone, removed, true, key, account, record);
		} else if (persistResult)
			for (const auto &md5 : removed)
				m_repository.remove(md5);
		return;
	}
	int row = 0;
	while (row < m_entries.size() && m_entries.at(row)->time > entry->time)
		++row;
	entry->id = replacementId ? replacementId : ++m_nextId;
	m_entries.insert(row, entry);
	if (persistResult) persist(entry, removed, false, key, account, record, true, retiredImageKeys);
	emit entryAdded(entry, row, HistoryChange::Synced);
}
