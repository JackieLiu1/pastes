#include "application/historyservice.h"
#include "core/clipboarddata.h"
#include "storage/database.h"
#include "ui/historyview.h"
#include "ui/searchbar.h"
#include "pastes_build_version.h"

#include <QApplication>
#include <QBuffer>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>

namespace {
void check(bool valid, const char *message)
{
	if (!valid) throw std::runtime_error(message);
}

/* Only fixture setup and inspection use SQL here. Production reads always
 * go through Database and its worker, on a separate, thread-owned connection. */
class FixtureConnection final
{
public:
	explicit FixtureConnection(const QString &path)
		: m_name(QUuid::createUuid().toString()),
		m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name))
	{
		m_database.setDatabaseName(path);
		if (!m_database.open()) {
			const QString error = m_database.lastError().text();
			m_database = QSqlDatabase();
			QSqlDatabase::removeDatabase(m_name);
			throw std::runtime_error(error.toStdString());
		}
	}
	~FixtureConnection()
	{
		m_database.close();
		m_database = QSqlDatabase();
		QSqlDatabase::removeDatabase(m_name);
	}
	FixtureConnection(const FixtureConnection &) = delete;
	FixtureConnection &operator=(const FixtureConnection &) = delete;
	QSqlDatabase &database(void) { return m_database; }

private:
	QString m_name;
	QSqlDatabase m_database;
};

QByteArray encodedImage(QRgb color, int width = 32, int height = 32)
{
	QImage image(width, height, QImage::Format_RGB32);
	image.fill(color);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	check(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"), "Cannot encode fixture PNG");
	return bytes;
}

QByteArray fixtureId(int row)
{
	return QCryptographicHash::hash(QByteArray("history benchmark ") + QByteArray::number(row), QCryptographicHash::Md5);
}

QByteArray binaryPayload(int row)
{
	return QByteArray("a\0b", 3) + QByteArray::number(row);
}

void seed(const QString &path, int count, bool mixed)
{
	FixtureConnection connection(path);
	QSqlQuery schema(connection.database());
	check(schema.exec("create table item(id integer primary key autoincrement, md5 blob, imagedata blob, icondata blob, time integer)"), "Cannot create fixture item table");
	check(schema.exec("create table data(id integer primary key autoincrement, md5 blob, formats text, format_data blob)"), "Cannot create fixture data table");
	check(schema.exec("create table favorite(md5 blob primary key, selected integer not null, modified integer not null, name text not null default '', name_modified integer not null default 0, position integer not null default 0, position_modified integer not null default 0)"), "Cannot create fixture favorite table");
	check(connection.database().transaction(), "Cannot start fixture transaction");
	QSqlQuery item(connection.database()), format(connection.database()), favorite(connection.database());
	check(item.prepare("insert into item(md5, imagedata, icondata, time) values(?, ?, ?, ?)"), "Cannot prepare fixture item");
	check(format.prepare("insert into data(md5, formats, format_data) values(?, ?, ?)"), "Cannot prepare fixture format");
	check(favorite.prepare("insert into favorite values(?, 1, ?, ?, ?, ?, ?)"), "Cannot prepare fixture favorite");
	const QByteArray icon = encodedImage(qRgb(50, 140, 110));
	const qint64 baseTime = QDateTime::currentSecsSinceEpoch()-86400;
	for (int row = 0; row < count; ++row) {
		const QByteArray id = fixtureId(row);
		QString text = QStringLiteral("Sample %1 剪贴板条目\n").arg(row, 6, 10, QLatin1Char('0')) + QString(160, QLatin1Char('x'));
		if (mixed && row%20 == 1) text += QString(65536, QLatin1Char('x')) + QStringLiteral(" long-text-tail");
		const bool image = mixed && row%20 == 0;
		item.bindValue(0, id);
		item.bindValue(1, image ? encodedImage(qRgb(row & 255, (row >> 8) & 255, 120), 320, 180) : QByteArray());
		item.bindValue(2, icon);
		/* Equal timestamps exercise the stable persisted insertion order. */
		item.bindValue(3, baseTime+row/4);
		check(item.exec(), "Cannot insert fixture item");
		auto write = [&](const QString &type, const QByteArray &bytes) {
			format.bindValue(0, id); format.bindValue(1, type); format.bindValue(2, bytes);
			check(format.exec(), "Cannot insert fixture format");
		};
		write(QStringLiteral("text/plain"), text.toUtf8());
		if (mixed && row%5 == 2) write(QStringLiteral("text/html"), (QStringLiteral("<b>")+text.toHtmlEscaped()+QStringLiteral("</b>")).toUtf8());
		if (image) write(QStringLiteral("application/x-qt-image"), QByteArray("", 0));
		write(QStringLiteral("application/x-pastes-benchmark-id"), binaryPayload(row));
		if (row%10 == 0) {
			favorite.bindValue(0, id); favorite.bindValue(1, baseTime*1000);
			favorite.bindValue(2, QStringLiteral("Saved %1").arg(row));
			favorite.bindValue(3, baseTime*1000); favorite.bindValue(4, (row+1)*1024);
			favorite.bindValue(5, baseTime*1000);
			check(favorite.exec(), "Cannot insert fixture favorite");
		}
	}
	check(connection.database().commit(), "Cannot commit fixture database");
}

QList<HistoryEntry> load(const QString &path, double &milliseconds)
{
	QElapsedTimer timer;
	timer.start();
	Database repository(path);
	QList<HistoryEntry> entries;
	QEventLoop loop;
	bool ready = false;
	QString error;
	QObject::connect(&repository, &HistoryRepository::loaded, &loop, [&](const QList<HistoryEntry> &loaded) {
		milliseconds = timer.nsecsElapsed()/1000000.0;
		entries = loaded;
		ready = true;
		loop.quit();
	});
	QObject::connect(&repository, &HistoryRepository::failed, &loop, [&](const QString &message) {
		error = message;
		loop.quit();
	});
	QTimer::singleShot(180000, &loop, &QEventLoop::quit);
	repository.load();
	loop.exec();
	if (!error.isEmpty()) throw std::runtime_error(error.toStdString());
	check(ready, "Database load timed out");
	return entries;
}

void validate(const QList<HistoryEntry> &entries, int count, bool mixed)
{
	check(entries.size() == count, "History benchmark lost entries");
	for (int index = 0; index < count; ++index) {
		const int row = count-index-1;
		const auto &entry = entries[index];
		check(entry->md5 == fixtureId(row), "Persisted history order changed");
		check(entry->mimeData->data("application/x-pastes-benchmark-id") == binaryPayload(row), "Binary format changed");
		check(entry->favorite == (row%10 == 0), "Favorite state changed");
		check(entry->mimeData->hasImage() == (mixed && row%20 == 0), "Image format changed");
		if (entry->mimeData->hasImage()) check(!ClipboardData::storedImage(entry->mimeData.get()).isEmpty(), "Image was eagerly expanded");
	}
}

QJsonArray queryPlan(const QString &path)
{
	FixtureConnection connection(path);
	QSqlQuery query(connection.database());
	query.prepare("explain query plan select formats, format_data from data where md5 = :md5 order by id asc;");
	query.bindValue(":md5", fixtureId(0));
	check(query.exec(), "Cannot inspect fixture query plan");
	QJsonArray plan;
	while (query.next()) plan.append(query.value(3).toString());
	return plan;
}

class SnapshotRepository final : public HistoryRepository
{
public:
	explicit SnapshotRepository(const QList<HistoryEntry> &entries) : m_entries(entries) {}
	void load(void) override { emit loaded(m_entries); }
	quint64 insert(const HistoryEntry &) override { return 0; }
	void remove(const QByteArray &) override {}
	void updateIcon(const QByteArray &, const QImage &) override {}
	void updateFavorite(const QByteArray &, bool, qint64, const FavoriteDetails &) override {}

private:
	QList<HistoryEntry> m_entries;
};

void measureView(const QList<HistoryEntry> &entries, QJsonObject &result)
{
	SnapshotRepository repository(entries);
	HistoryService history(repository);
	QWidget window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.resize(1200, 400);
	QVBoxLayout layout(&window);
	QElapsedTimer timer;
	timer.start();
	HistoryView view(history, &window);
	layout.addWidget(&view);
	history.load();
	layout.activate();
	auto *list = view.findChild<QListWidget *>();
	check(list && list->count() == entries.size() && history.entries().size() == entries.size(), "History view lost benchmark entries");
	list->doItemsLayout();
	result.insert("card_bind_ms", timer.nsecsElapsed()/1000000.0);
	result.insert("widget_count", window.findChildren<QWidget *>().size());
	window.show();
	timer.restart();
	check(!view.grab().isNull(), "Cannot render fixture viewport");
	result.insert("offscreen_render_ms", timer.nsecsElapsed()/1000000.0);
	auto *search = view.findChild<LineEdit *>();
	check(search, "Cannot find benchmark search field");
	QJsonArray samples;
	const QStringList queries{QStringLiteral("Sample"), QStringLiteral("not-present-anywhere"),
		QStringLiteral("剪贴板"), QStringLiteral("long-text-tail"), QString()};
	for (int repeat = 0; repeat < 5; ++repeat) {
		for (const QString &query : queries) {
			timer.restart();
			search->setText(query);
			list->doItemsLayout();
			const double elapsed = timer.nsecsElapsed()/1000000.0;
			int visible = 0;
			for (int row = 0; row < list->count(); ++row) if (!list->item(row)->isHidden()) ++visible;
			const int expected = query == QStringLiteral("not-present-anywhere") ? 0 :
				(query == QStringLiteral("long-text-tail") ?
				 static_cast<int>(std::count_if(entries.cbegin(), entries.cend(), [](const HistoryEntry &entry) {
					 return entry->mimeData->text().contains(QStringLiteral("long-text-tail"));
				 })) : static_cast<int>(entries.size()));
			check(visible == expected, "Search benchmark returned incorrect results");
			samples.append(QJsonObject{{"query", query}, {"elapsed_ms", elapsed}, {"matches", visible}});
		}
	}
	result.insert("search_samples", samples);
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QCoreApplication::setOrganizationName(QStringLiteral("PastesBenchmark"));
	QCoreApplication::setApplicationName(QStringLiteral("HistoryBenchmark"));
	QCommandLineParser parser;
	parser.addHelpOption();
	parser.addOption({"entries", "Number of synthetic history entries (1-10000).", "count", "1000"});
	parser.addOption({"profile", "Fixture profile: text or mixed.", "profile", "text"});
	parser.addOption({"stage", "Measure database only, or database plus view.", "stage", "view"});
	parser.process(app);
	try {
		bool validCount = false;
		const int count = parser.value("entries").toInt(&validCount);
		const QString profile = parser.value("profile"), stage = parser.value("stage");
		check(validCount && count > 0 && count <= 10000, "Entry count must be between 1 and 10000");
		check(profile == "text" || profile == "mixed", "Unknown fixture profile");
		check(stage == "database" || stage == "view", "Unknown benchmark stage");
		QTemporaryDir directory;
		check(directory.isValid(), "Cannot create isolated benchmark directory");
		QSettings::setDefaultFormat(QSettings::IniFormat);
		QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
		const QString path = directory.filePath("fixture.db");
		seed(path, count, profile == "mixed");
		double first = 0, reopen = 0;
		auto entries = load(path, first);
		validate(entries, count, profile == "mixed");
		entries.clear();
		entries = load(path, reopen);
		validate(entries, count, profile == "mixed");
		QJsonObject result{{"revision", PASTES_GIT_REVISION}, {"qt_version", qVersion()},
			{"build_type", PASTES_BENCHMARK_BUILD_TYPE}, {"qpa_platform", QGuiApplication::platformName()},
			{"entries", count}, {"profile", profile}, {"stage", stage},
			{"first_open_load_ms", first}, {"reopen_load_ms", reopen},
			{"database_bytes", QFileInfo(path).size()}, {"data_lookup_plan", queryPlan(path)}};
		if (stage == "view") measureView(entries, result);
		QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << '\n';
		return 0;
	} catch (const std::exception &error) {
		QTextStream(stderr) << "History benchmark failed: " << error.what() << '\n';
		return 1;
	}
}
