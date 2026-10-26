#include "tests/application/memoryrepository.h"
#include "tests/support/colorcontrast.h"
#include "application/clipboardcontroller.h"
#include "ui/window/mainwindow.h"
#include "ui/history/historyview.h"
#include "ui/cards/pasteitem.h"
#include "ui/preview/previewdialog.h"
#include "ui/dialogs/appdialog.h"
#include "ui/dialogs/settingsdialog.h"
#include "ui/common/roundedwidgets.h"
#include "platform/windowintegration.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QScrollBar>
#include <QTemporaryDir>
#include <memory>

namespace {
class IdleFeed final : public ClipboardFeed
{
public:
	int settleInterval(void) const override { return 0; }
	void capture(quint64) override {}
	bool synchronize(void) override { return false; }
	bool allowsCapture(void) const override { return false; }
	QImage snapshotIcon(void) override { return {}; }
};

struct WindowFixture
{
	MemoryRepository repository;
	HistoryService history{repository};
	IdleFeed feed;
	ClipboardController clipboard{history, feed, *QApplication::clipboard(), false};
	MainWindow window{history, clipboard};
};

void historyLoadKeepsWindowState(void)
{
	class VisibilityEvents final : public QObject
	{
	public:
		int shows = 0;
		int hides = 0;
	protected:
		bool eventFilter(QObject *, QEvent *event) override
		{
			if (event->type() == QEvent::Show) ++shows;
			if (event->type() == QEvent::Hide) ++hides;
			return false;
		}
	};
	for (bool visible : {false, true}) {
		WindowFixture fixture;
		auto &window = fixture.window;
		auto *list = window.findChild<QListWidget *>();
		auto *search = window.findChild<QLineEdit *>();
		if (visible) {
			window.show_window(); window.activateWindow(); search->setFocus();
			waitUntil([&] { return search->hasFocus(); });
		}
		VisibilityEvents events;
		window.installEventFilter(&events);
		fixture.history.load();
		QTimer::singleShot(40, &window, [&] {
			fixture.repository.finishLoad({textEntry("startup first"), textEntry("startup second")});
		});
		waitUntil([&] { return list->count() == 2; });
		require(events.shows == 0 && events.hides == 0 && window.isVisible() == visible,
			"History load automatically showed or hid the panel");
		if (visible) require(search->hasFocus(), "History load stole the existing search focus");
	}
}

void trayPanelActionFollowsVisibility(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	auto *action = window.findChild<QAction *>("ShowHistoryAction");
	require(action, "Tray history action missing");
	const QString showLabel = action->text();
	action->trigger();
	require(window.isVisible(), "Tray action did not open history");
	require(action->text() != showLabel, "Visible history still offers a show action");
	action->trigger();
	require(action->text() == showLabel, "Hide animation did not update the tray action");
	action->trigger(); // Reverse the pending hide before QWidget becomes hidden.
	require(window.isVisible() && action->text() != showLabel,
		"Reopening during hide left the tray action stale");
	window.toggle_window(); // The same entry point used by the global shortcut.
	waitUntil([&] { return !window.isVisible(); });
	require(action->text() == showLabel, "Shortcut dismissal left the tray action stale");
	window.show_window();
	window.hide(); // Secondary instances and immediate platform dismissal.
	require(action->text() == showLabel, "Direct dismissal left a hide action");
	window.toggle_window();
	require(window.isVisible() && action->text() != showLabel,
		"Shortcut toggle could not reopen after direct dismissal");
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
}

void openingFollowsHiddenCopies(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	auto *view = window.findChild<HistoryView *>();
	auto *list = view->findChild<QListWidget *>();
	auto selected = [&] {
		return qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry();
	};
	const auto now = QDateTime::currentDateTime();
	QList<HistoryEntry> entries;
	for (int i = 0; i < 12; ++i)
		entries.append(textEntry(QString("local %1").arg(i), now.addSecs(-10-i)));
	fixture.history.load();
	fixture.repository.finishLoad(entries);
	auto remote = textEntry("startup remote", now);
	fixture.history.mergeSynced(remote, {});
	require(!window.isVisible() && selected() == entries.first(),
		"Hidden startup sync showed the panel or changed its current selection");
	window.show_window();
	require(selected() == remote && list->horizontalScrollBar()->value() == 0,
		"First opening did not select the newest synchronized copy");
	list->setCurrentRow(list->count()-1);
	view->focusCurrent();
	const auto chosen = selected();
	const int position = list->horizontalScrollBar()->value();
	fixture.history.mergeSynced(textEntry("visible remote", now.addSecs(1)), {});
	require(selected() == chosen && list->horizontalScrollBar()->value() == position,
		"Visible sync changed the selected content or browsing position");
	window.hide();
	window.show_window();
	require(selected() == chosen, "Reopening without hidden arrivals reset selection");
	window.hide();
	fixture.history.mergeSynced(textEntry("older remote", now.addDays(-1)), {});
	window.show_window();
	require(selected() == chosen, "An older synchronized record reset selection");
	window.hide();
	auto newest = textEntry("hidden newest remote", now.addSecs(2));
	fixture.history.mergeSynced(newest, {});
	require(selected() == chosen, "A hidden arrival changed selection before opening");
	window.show_window();
	require(selected() == newest && list->horizontalScrollBar()->value() == 0,
		"Reopening after a hidden leading copy did not select and reveal it");
	window.hide_window();
	window.show_window();
	require(selected() == newest, "Reversing dismissal changed the selected copy");
	list->setCurrentRow(list->count()-2);
	const auto beforeDeletedArrival = selected();
	window.hide();
	auto temporary = textEntry("removed before opening", now.addSecs(3));
	fixture.history.mergeSynced(temporary, {});
	fixture.history.mergeSynced(nullptr, {temporary->md5});
	window.show_window();
	require(selected() == beforeDeletedArrival,
		"An arrival deleted before opening still reset the browsing selection");
	window.hide();
	auto surviving = textEntry("surviving hidden arrival", now.addSecs(4));
	fixture.history.mergeSynced(surviving, {});
	auto removed = textEntry("removed newer hidden arrival", now.addSecs(5));
	fixture.history.mergeSynced(removed, {});
	fixture.history.mergeSynced(nullptr, {removed->md5});
	window.show_window();
	require(selected() == surviving,
		"Deleting the newest hidden arrival forgot a surviving new leading copy");
	window.hide();
}

void metadataDoesNotResetOpening(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	auto *list = window.findChild<QListWidget *>();
	const auto now = QDateTime::currentDateTime();
	auto head = textEntry("metadata head", now);
	auto chosen = textEntry("browsing selection", now.addSecs(-10));
	fixture.history.load();
	fixture.repository.finishLoad({head, chosen});
	window.show_window();
	list->setCurrentRow(1);
	window.hide();
	/* Portable MIME normalization can replace the local identity without
	 * creating a new copy event. It must not request a jump to the head. */
	auto normalized = cloneEntry(*head);
	normalized->md5 = QByteArray(16, 'n');
	fixture.history.mergeSynced(normalized, {head->md5});
	fixture.history.setFavorite(normalized->id, true);
	window.show_window();
	require(qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == chosen,
		"Payload normalization or favorite metadata reset the next opening");
	window.hide();
	fixture.history.mergeSynced(cloneEntry(*normalized), {});
	window.show_window();
	require(qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == chosen,
		"A replay of the same copy reset the next opening");
	window.hide();
	auto recopied = cloneEntry(*normalized);
	recopied->time = now.addSecs(10);
	recopied->events.copy = nextVersion(normalized->events.copy);
	fixture.history.mergeSynced(recopied, {});
	window.show_window();
	require(qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == recopied,
		"A new copy of existing content did not refresh the next opening");
	fixture.history.mergeSynced(nullptr, {recopied->md5});
	require(qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == chosen,
		"Deleting the selected copy did not select its surviving neighbor");
	fixture.history.mergeSynced(nullptr, {chosen->md5});
	require(list->count() == 0 && !list->currentItem(),
		"Deleting the final copy retained a stale selection");
	window.hide();
}

void openingPreservesFilteredContext(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	auto *list = window.findChild<QListWidget *>();
	auto *search = window.findChild<QLineEdit *>();
	const auto now = QDateTime::currentDateTime();
	auto alpha = textEntry("saved alpha", now.addSecs(-20));
	auto beta = textEntry("saved beta", now.addSecs(-30));
	fixture.history.load();
	fixture.repository.finishLoad({alpha, beta});
	fixture.history.setFavorite(alpha->id, true);
	fixture.history.setFavorite(beta->id, true);
	window.show_window();
	search->setText("saved");
	list->setCurrentRow(1);
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
	fixture.history.mergeSynced(textEntry("saved newest", now), {});
	window.show_window();
	window.activateWindow();
	require(window.isVisible(), "Reopening the search panel immediately hid it again");
	// Offscreen validates the input target; native activation is a desktop contract.
	require(window.focusWidget() == search, "Reopening did not restore the search input target");
	if (QGuiApplication::platformName() != QStringLiteral("offscreen"))
		waitUntil([&] { return search->hasFocus(); });
	require(search->text() == "saved" &&
			qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == beta,
		"A hidden arrival reset the search selection or query");
	search->clear();
	window.findChild<QPushButton *>("FavoritesTab")->click();
	list->setCurrentRow(1);
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
	fixture.history.mergeSynced(textEntry("ordinary newest", now.addSecs(1)), {});
	window.show_window();
	require(window.findChild<QPushButton *>("FavoritesTab")->isChecked() &&
			qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == beta,
		"A hidden history arrival reset the favorites context");
	window.hide();
}

void backdropUsesOpaqueFallback(void)
{
	QWidget panel;
	panel.resize(160, 100);
	RoundedSurface surface;
	for (bool dark : {false, true}) {
		qApp->setProperty("pastesDark", dark);
		for (bool active : {false, true, false}) {
			panel.setProperty("pastesPanelBackdrop", active);
			QImage image(panel.size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(Qt::transparent);
			QPainter painter(&image);
			surface.paint(&panel, RoundedRole::Panel, painter);
			painter.end();
			const int alpha = image.pixelColor(80, 50).alpha();
			if (Platform::panelAppearance().nativeBackdrop) {
				if (active) require(alpha > 0 && alpha < 255, "Active backdrop is covered by opaque paint");
				else require(alpha == 255, "Unavailable backdrop left a translucent panel");
			}
			require(image.pixelColor(3, 3).alpha() == 0 &&
				image.pixelColor(156, 3).alpha() == 0 &&
				image.pixelColor(15, 2).alpha() > 0 &&
				image.pixelColor(144, 2).alpha() > 0,
				"Panel did not retain its 18 DIP custom top corners");
			require(image.pixelColor(2, 97).alpha() > 0 &&
				image.pixelColor(157, 97).alpha() > 0,
				"Panel rounded a bottom corner");
			QPainter cardPainter(&image);
			surface.paint(&panel, RoundedRole::Card, cardPainter);
			cardPainter.end();
			require(image.pixelColor(80, 50).alpha() == 255, "Glass made card contents translucent");
		}
	}
	qApp->setProperty("pastesDark", false);
}

void appDialogsHideHistory(void)
{
	WindowFixture fixture;
	auto &window = fixture.window;
	for (const char *actionName : {"SettingsAction", "AboutAction"}) {
		auto *action = window.findChild<QAction *>(QString::fromLatin1(actionName));
		require(action, "Application dialog action missing");
		for (bool fromPanel : {true, false}) {
			if (fromPanel) window.show_window();
			bool inspected = false, hidden = false, reopeningBlocked = false;
			QTimer inspection;
			inspection.setSingleShot(true);
			QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || !dialog->isVisible()) return;
				inspected = true;
				hidden = !window.isVisible();
				window.show_window(); // Shared tray and global-shortcut entry point.
				reopeningBlocked = !window.isVisible();
				if (auto *close = dialog->findChild<QPushButton *>("PreviewClose")) close->click();
				else dialog->reject();
			});
			inspection.start(350); // Wait for the panel's 200 ms hide animation.
			QTimer watchdog;
			watchdog.setSingleShot(true);
			QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
				if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
					dialog->reject();
			});
			watchdog.start(2000);
			action->trigger();
			require(inspected, "Application dialog did not stay open");
			require(hidden, "Application dialog left the history panel visible");
			require(reopeningBlocked, "History reopened over an application dialog");
			require(!window.isVisible(), "Closing an application dialog reopened history");
		}
		window.show_window();
		require(window.isVisible(), "History could not reopen after closing the dialog");
		window.hide_window();
		waitUntil([&] { return !window.isVisible(); });
	}
}

void lightThemeTextContrast(void)
{
	QSettings().setValue("theme", "light");
	WindowFixture fixture;
	fixture.history.load();
	fixture.repository.finishLoad({textEntry("Readable clipboard text")});
	auto &window = fixture.window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.show_window();
	auto *view = window.findChild<HistoryView *>();
	require(view, "History view missing");
	view->ensurePolished();
	RoundedSurface surface;
	for (bool active : {false, true}) {
		window.setProperty("pastesPanelBackdrop", active);
		for (const QColor &background : {QColor(Qt::black), QColor(Qt::white)}) {
			QImage image(view->size(), QImage::Format_ARGB32_Premultiplied);
			image.fill(background);
			QPainter painter(&image);
			surface.paint(view, RoundedRole::Panel, painter);
			painter.end();
			const QColor panelColor = image.pixelColor(view->width()/2, view->height()/2);
			for (const char *name : {"BrandTitle", "HistoryTab", "FavoritesTab", "HistoryCount", "KeyboardHint",
				"EmptyState", "RecordingStatus", "UndoHint", "UndoButton", "PanelMenu"}) {
				auto *label = view->findChild<QWidget *>(name);
				require(label, (std::string("Panel text widget missing: ")+name).c_str());
				label->ensurePolished();
				const QColor ink = label->palette().color(QPalette::WindowText);
				require(contrastRatio(ink, panelColor) >= 4.5,
					(std::string("Light panel text lost contrast: ")+name).c_str());
			}
		}
	}
	auto *card = view->findChild<QWidget *>("PasteItemFrame");
	require(card, "Contrast test did not bind the history card");
	QImage image(card->size(), QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	surface.paint(card, RoundedRole::Card, painter);
	painter.end();
	const QColor cardColor = image.pixelColor(card->width()/2, card->height()/2);
	for (QLabel *label : card->findChildren<QLabel *>()) {
		if (label->text().isEmpty()) continue;
		label->ensurePolished();
		require(contrastRatio(label->palette().color(QPalette::WindowText), cardColor) >= 4.5,
			(std::string("Light card metadata lost contrast: ")+label->objectName().toStdString()).c_str());
	}
}

void favoriteNameDialogCommands(void)
{
	WindowFixture fixture; auto item = textEntry("unchanged payload");
	fixture.history.load(); fixture.repository.finishLoad({item});
	fixture.history.setFavorite(item->id, true);
	fixture.history.setFavoriteName(item->id, "original");
	QEventLoop startup; QTimer::singleShot(50, &startup, &QEventLoop::quit); startup.exec();
	auto &window = fixture.window; auto *view = window.findChild<HistoryView *>();
	for (int mode : {0, 1, 2}) {
		window.show_window();
		bool inspected = false, blocked = false, validInput = false;
		QTimer inspection;
		QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
			auto *dialog = dynamic_cast<FavoriteNameDialog *>(QApplication::activeModalWidget());
			if (!dialog) return;
			inspected = true;
			auto *edit = dialog->findChild<QLineEdit *>("FavoriteNameEdit");
			validInput = edit && edit->maxLength() == FavoriteDetails::maxNameLength;
			window.toggle_window(); blocked = window.isVisible();
			if (edit) edit->setText(mode == 2 ? "" : "edited name");
			if (mode == 0) dialog->reject(); else dialog->accept();
		});
		inspection.start(20);
		QTimer::singleShot(1500, &window, [] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
		});
		view->renameFavoriteRequested(item);
		inspection.stop();
		require(inspected, "Name dialog did not open");
		require(validInput, "Name dialog lost its input length limit");
		require(blocked, "Panel toggle was not blocked while naming");
		require(window.isVisible(), "Name dialog did not preserve the visible panel");
		require(item->favoriteDetails.name == (mode == 0 ? "original" : mode == 1 ? "edited name" : ""),
			"Name dialog save, cancel or clear command failed");
		require(item->mimeData->text() == "unchanged payload", "Name dialog modified clipboard content");
	}
	window.hide();
}

void previewKeepsHistoryVisible(void)
{
	WindowFixture fixture;
	auto entry = textEntry("preview with visible history");
	fixture.history.load();
	fixture.repository.finishLoad({entry});
	QEventLoop startup;
	QTimer::singleShot(50, &startup, &QEventLoop::quit);
	startup.exec();
	auto &window = fixture.window;
	auto *view = window.findChild<HistoryView *>();
	require(view, "History view missing");
	auto *list = view->findChild<QListWidget *>();
	require(list && list->currentItem(), "Preview selection missing");
	for (int mode : {0, 1, 2}) {
		window.show_window();
		/* Deliver the card command directly: offscreen has no native panel
		 * activation. Panel/list key routing is covered by historyview_test. */
		QWidget *receiver = list->itemWidget(list->currentItem());
		require(receiver, "Preview key receiver missing");
		receiver->setFocus();
		bool inspected = false, panelVisible = false, focused = false;
		bool stackingMatches = true, heldSpaceKeptOpen = false, closedWithKey = mode == 0;
		QTimer inspection;
		inspection.setSingleShot(true);
		QObject::connect(&inspection, &QTimer::timeout, &window, [&] {
			auto *dialog = qobject_cast<PreviewDialog *>(QApplication::activeModalWidget());
			if (!dialog || !dialog->isVisible()) return;
			inspected = true;
			panelVisible = window.isVisible();
			if (mode == 0)
				fixture.history.mergeSynced(textEntry("arrived while previewing",
					QDateTime::currentDateTime().addSecs(5)), {});
			focused = dialog->isActiveWindow();
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
			const auto stacking = window.windowFlags() & (Qt::WindowStaysOnTopHint | Qt::BypassWindowManagerHint);
			stackingMatches = (dialog->windowFlags() & stacking) == stacking;
#endif
			QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
			QApplication::sendEvent(dialog, &repeat);
			heldSpaceKeptOpen = dialog->isVisible();
			if (mode == 1) {
				QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
				QApplication::sendEvent(dialog, &key);
				closedWithKey = !dialog->isVisible();
			} else if (mode == 2) {
				QKeyEvent key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
				QApplication::sendEvent(dialog, &key);
				closedWithKey = !dialog->isVisible();
			} else if (auto *close = dialog->findChild<QPushButton *>("PreviewClose")) close->click();
			else dialog->reject();
		});
		inspection.start(350);
		QTimer watchdog;
		watchdog.setSingleShot(true);
		QObject::connect(&watchdog, &QTimer::timeout, &window, [] {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				dialog->reject();
		});
		watchdog.start(2000);
		QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
		QApplication::sendEvent(receiver, &space);
		require(inspected && panelVisible, "Preview hid its history panel");
		require(focused, "Preview did not take keyboard focus");
		require(stackingMatches, "Preview did not share its panel's topmost stacking policy");
		require(heldSpaceKeptOpen, "Holding Space closed the newly opened preview");
		require(closedWithKey, "Escape or Space did not close the preview");
		require(window.isVisible(), "Closing preview hid its history panel");
		require(qobject_cast<PasteItem *>(list->itemWidget(list->currentItem()))->entry() == entry,
			"A sync arrival changed the selection when returning from preview");
	}
	window.hide_window();
	waitUntil([&] { return !window.isVisible(); });
}

void previewHeaderDrag(void)
{
	QWidget panel;
	Platform::initializePanel(&panel);
	auto entry = textEntry("Selectable preview text");
	PreviewDialog dialog(*entry, &panel);
	dialog.show();
	dialog.move(120, 140);
	auto *title = dialog.findChild<QLabel *>("PreviewTitle");
	auto *text = dialog.findChild<QPlainTextEdit *>("PreviewText");
	require(title && text, "Preview drag controls missing");
	QWidget *header = title->parentWidget();
	auto mouse = [](QWidget *receiver, QEvent::Type type, const QPoint &global,
		Qt::MouseButton button, Qt::MouseButtons buttons) {
		QMouseEvent event(type, receiver->mapFromGlobal(global), global,
			button, buttons, Qt::NoModifier);
		QApplication::sendEvent(receiver, &event);
	};
	const QPoint original = dialog.pos();
	const QPoint press = header->mapToGlobal(header->rect().center());
	const QPoint delta(80, -35);
	mouse(header, QEvent::MouseButtonPress, press, Qt::LeftButton, Qt::LeftButton);
	mouse(&dialog, QEvent::MouseMove, press+delta, Qt::NoButton, Qt::LeftButton);
	require(dialog.pos() == original+delta, "Unmanaged preview did not follow its header drag");
	mouse(&dialog, QEvent::MouseButtonRelease, press+delta, Qt::LeftButton, Qt::NoButton);
	mouse(&dialog, QEvent::MouseMove, press+delta*2, Qt::NoButton, Qt::LeftButton);
	require(dialog.pos() == original+delta, "Preview kept dragging after mouse release");
	const QPoint content = text->viewport()->mapToGlobal(text->viewport()->rect().center());
	mouse(text->viewport(), QEvent::MouseButtonPress, content, Qt::LeftButton, Qt::LeftButton);
	mouse(text->viewport(), QEvent::MouseMove, content+delta, Qt::NoButton, Qt::LeftButton);
	mouse(text->viewport(), QEvent::MouseButtonRelease, content+delta, Qt::LeftButton, Qt::NoButton);
	require(dialog.pos() == original+delta, "Text selection moved the preview window");
	const QPoint secondPress = header->mapToGlobal(header->rect().center());
	mouse(header, QEvent::MouseButtonPress, secondPress, Qt::LeftButton, Qt::LeftButton);
	dialog.hide();
	dialog.show();
	const QPoint reopened = dialog.pos();
	mouse(&dialog, QEvent::MouseMove, secondPress+delta, Qt::NoButton, Qt::LeftButton);
	require(dialog.pos() == reopened, "Reopened preview retained an interrupted drag");
	dialog.hide();
}

void dialogGlassContrast(bool dark)
{
	QFile theme(dark ? ":/resources/theme-dark.qss" : ":/resources/theme-light.qss");
	require(theme.open(QIODevice::ReadOnly), "Dialog contrast test could not load the theme");
	qApp->setProperty("pastesDark", dark);
	auto entry = textEntry("Readable preview text");
	std::unique_ptr<QDialog> dialogs[] = {
		std::make_unique<AboutDialog>(), std::make_unique<SettingsDialog>("Win+V"),
		std::make_unique<PreviewDialog>(*entry)};
	const QString stylesheet = QString::fromUtf8(theme.readAll());
	for (auto &dialog : dialogs) {
		dialog->setAttribute(Qt::WA_DontShowOnScreen);
		dialog->setStyleSheet(stylesheet);
		dialog->show();
		auto *surface = dialog->findChild<QWidget *>("AppDialogSurface");
		if (!surface) surface = dialog->findChild<QWidget *>("PreviewSurface");
		require(surface, "Dialog surface missing");
		RoundedSurface paint;
		for (bool active : {false, true, false}) {
			dialog->setProperty("pastesDialogBackdrop", active);
			QImage tint(surface->size(), QImage::Format_ARGB32_Premultiplied);
			tint.fill(Qt::transparent);
			QPainter painter(&tint);
			paint.paint(surface, RoundedRole::Preview, painter);
			painter.end();
			const QColor center = tint.pixelColor(tint.width()/2, tint.height()/2);
			require(active ? center.alpha() < 192 : center.alpha() == 255,
				"Dialog did not switch between visible glass and opaque fallback");
			for (int x : {3, tint.width()-4})
				for (int y : {3, tint.height()-4})
					require(tint.pixelColor(x, y).alpha() == 0, "Dialog lost a custom corner");
			for (const QColor &background : {QColor(Qt::black), QColor(Qt::white)}) {
				QImage composite(tint.size(), tint.format()); composite.fill(background);
				QPainter blend(&composite); blend.drawImage(QPoint(), tint); blend.end();
				const QColor glass = composite.pixelColor(tint.width()/2, tint.height()/2);
				for (QLabel *label : dialog->findChildren<QLabel *>()) {
					if (label->text().isEmpty() || !label->isVisibleTo(dialog.get())) continue;
					bool opaque = label->objectName() == "AboutVersion";
					for (QWidget *parent = label->parentWidget(); parent && parent != surface; parent = parent->parentWidget())
						if (parent->objectName() == "AppDialogCard" || parent->objectName() == "PreviewContent") opaque = true;
					if (opaque) continue;
					label->ensurePolished();
					require(contrastRatio(label->palette().color(QPalette::WindowText), glass) >= 4.5,
						(std::string("Dialog glass text lost contrast: ")+label->objectName().toStdString()).c_str());
				}
			}
		}
		dialog->hide();
	}
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	QTemporaryDir preferences;
	QCoreApplication::setOrganizationName("PastesWindowTests");
	QCoreApplication::setApplicationName("Dialogs");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
	if (app.arguments().contains("--startup-only"))
		return runTest("delayed history load preserves window state and focus", historyLoadKeepsWindowState);
	if (app.arguments().contains("--selection-only")) {
		int failures = runTest("normal opening follows hidden leading copies", openingFollowsHiddenCopies);
		failures += runTest("metadata and replay preserve the next opening", metadataDoesNotResetOpening);
		failures += runTest("normal opening preserves search and favorites", openingPreservesFilteredContext);
		return failures ? 1 : 0;
	}
	if (app.arguments().contains("--preview-only")) {
		int failures = runTest("preview header drag preserves text selection and cancels on close", previewHeaderDrag);
		failures += runTest("Space preview takes focus and closes with Escape or Space", previewKeepsHistoryVisible);
		return failures ? 1 : 0;
	}
	int failures = runTest("tray history action follows visibility and pending animations", trayPanelActionFollowsVisibility);
	failures += runTest("normal opening follows hidden leading copies", openingFollowsHiddenCopies);
	failures += runTest("metadata and replay preserve the next opening", metadataDoesNotResetOpening);
	failures += runTest("normal opening preserves search and favorites", openingPreservesFilteredContext);
	failures += runTest("delayed history load preserves window state and focus", historyLoadKeepsWindowState);
	failures += runTest("backdrop activation and fallback preserve readable surfaces", backdropUsesOpaqueFallback);
	failures += runTest("light theme text remains readable over black and white backgrounds", lightThemeTextContrast);
	failures += runTest("application dialogs hide history and block reopening", appDialogsHideHistory);
	failures += runTest("Space preview takes focus, stays above history and closes with Escape or Space", previewKeepsHistoryVisible);
	failures += runTest("preview header drag preserves text selection and cancels on close", previewHeaderDrag);
	failures += runTest("favorite name dialog saves, clears, cancels and blocks panel toggles", favoriteNameDialogCommands);
	failures += runTest("dialog glass keeps readable text and opaque fallback", [] {
		dialogGlassContrast(false); dialogGlassContrast(true);
	});
	return failures ? 1 : 0;
}
