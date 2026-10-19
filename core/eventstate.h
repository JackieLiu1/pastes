#ifndef PASTES_EVENTSTATE_H
#define PASTES_EVENTSTATE_H

#include <QDateTime>
#include <QJsonObject>
#include <QUuid>
#include <limits>

/* Persistent hybrid logical versions. Wall time on ItemData is only display
 * and retention time; callbacks and transport never create another copy. */
struct EventVersion {
	static constexpr qint64 maxLogical = 1000000000;
	static constexpr qint64 maxPhysical = std::numeric_limits<qint64>::max() / 2;
	qint64 physical = 0;
	qint64 logical = 0;
	QString device;
	bool valid(void) const { return physical > 0; }
	bool operator==(const EventVersion &b) const
	{
		return physical == b.physical && logical == b.logical && device == b.device;
	}
	bool operator!=(const EventVersion &b) const { return !(*this == b); }
	bool operator<(const EventVersion &b) const
	{
		return physical != b.physical
			? physical < b.physical
			: (logical != b.logical ? logical < b.logical : device < b.device);
	}
	bool operator>(const EventVersion &b) const { return b < *this; }
	bool operator>=(const EventVersion &b) const { return !(*this < b); }
	QJsonObject json(void) const
	{
		return {{"time", physical}, {"counter", logical}, {"device", device}};
	}
	static EventVersion parse(const QJsonObject &o)
	{
		return {o.value("time").toInteger(), o.value("counter").toInteger(),
			o.value("device").toString()};
	}
	static EventVersion legacy(qint64 time) { return {qMax(qint64(0), time), 0, {}}; }
};
Q_DECLARE_METATYPE(EventVersion)

struct EventState {
	EventVersion copy, erase, source, favorite, name, position;
	qint64 sourceCaptured = 0;
	int sourceRevision = 0;
	qint64 erasedAt = 0;
	bool deleted(void) const { return erase.valid() && erase >= copy; }
	EventVersion latest(void) const
	{
		return qMax(qMax(qMax(copy, erase), qMax(source, favorite)), qMax(name, position));
	}
	void observe(const EventState &other)
	{
		copy = qMax(copy, other.copy);
		if (other.erase > erase) {
			erase = other.erase;
			erasedAt = other.erasedAt;
		}
		if (other.source > source ||
			(other.source == source && other.sourceRevision > sourceRevision)) {
			source = other.source;
			sourceCaptured = other.sourceCaptured;
			sourceRevision = other.sourceRevision;
		}
		favorite = qMax(favorite, other.favorite);
		name = qMax(name, other.name);
		position = qMax(position, other.position);
	}
	QJsonObject json(void) const
	{
		return {{"copy", copy.json()}, {"erase", erase.json()}, {"source", source.json()},
			{"sourceCaptured", sourceCaptured}, {"sourceRevision", sourceRevision},
			{"erasedAt", erasedAt}, {"favorite", favorite.json()}, {"name", name.json()},
			{"position", position.json()}};
	}
	static EventState parse(const QJsonObject &o)
	{
		EventState s;
		s.copy = EventVersion::parse(o.value("copy").toObject());
		s.erase = EventVersion::parse(o.value("erase").toObject());
		s.source = EventVersion::parse(o.value("source").toObject());
		s.sourceCaptured = o.value("sourceCaptured").toInteger();
		s.sourceRevision = o.value("sourceRevision").toInt();
		s.erasedAt = o.value("erasedAt").toInteger();
		s.favorite = EventVersion::parse(o.value("favorite").toObject());
		s.name = EventVersion::parse(o.value("name").toObject());
		s.position = EventVersion::parse(o.value("position").toObject());
		return s;
	}
};
Q_DECLARE_METATYPE(EventState)

class EventClock
{
public:
	QString device = QUuid::createUuid().toString(QUuid::WithoutBraces);
	EventVersion last;
	void observe(const EventVersion &v)
	{
		if (v > last) last = v;
	}
	EventVersion tick(qint64 wall = QDateTime::currentMSecsSinceEpoch())
	{
		auto physical = qMax(wall, last.physical);
		auto logical = wall > last.physical ? 0 : last.logical + 1;
		if (logical > EventVersion::maxLogical) {
			++physical;
			logical = 0;
		}
		last = {physical, logical, device};
		return last;
	}
};
#endif
