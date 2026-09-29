#include "tests/testsupport.h"
#include "ui/historyview.h"
#include "ui/pasteitem.h"
#include "ui/searchbar.h"
#include "ui/cardinteraction.h"
#include "ui/cardswipe.h"
#include <QApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QShortcut>
#include <QVBoxLayout>

void viewCommands(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.resize(1200, 360);
	auto *view = new HistoryView(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(view);
	history.load();
	repository.finishLoad({textEntry("first"), textEntry("second"), textEntry("third")});
	auto *list = view->findChild<QListWidget *>();
	require(list && list->count() == 3, "History cards did not bind to the service");
	auto card = [&](int row) { return qobject_cast<PasteItem *>(list->itemWidget(list->item(row))); };
	HistoryEntry copied;
	bool plain = false;
	QObject::connect(view, &HistoryView::copyRequested, view,
		[&](HistoryEntry entry, bool plainText, bool) { copied = entry; plain = plainText; });
	QKeyEvent copy(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier);
	QCoreApplication::sendEvent(card(0), &copy);
	require(copied && copied->mimeData->text() == "first" && plain, "Card copy command lost its typed payload");
	QKeyEvent remove(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
	QCoreApplication::sendEvent(card(0), &remove);
	require(list->count() == 2 && history.entries().size() == 2, "Delete command did not update model and view");
	require(copied->mimeData->text() == "first", "Removed card invalidated an independent consumer");
	QShortcut *undo = nullptr;
	for (QShortcut *shortcut : view->findChildren<QShortcut *>())
		if (shortcut->key() == QKeySequence("Ctrl+Z")) undo = shortcut;
	require(undo && undo->isEnabled(), "Undo action unavailable after deletion");
	QMetaObject::invokeMethod(undo, "activated", Qt::DirectConnection);
	require(list->count() == 3 && card(0)->entry()->mimeData->text() == "first", "Undo did not restore the first card");
	auto *search = view->findChild<LineEdit *>();
	search->setText("second");
	require(list->item(0)->isHidden() && !list->item(1)->isHidden() && list->item(2)->isHidden(), "Search filtering changed");
	history.discard(history.entries().first()->id);
	search->clear();
	require(list->count() == 2 && !list->item(0)->isHidden(), "Search retained a removed selection");

	/* Invalid persisted content is removed without dangling item pointers. */
	MemoryRepository otherRepository;
	HistoryService otherHistory(otherRepository);
	QWidget otherWindow;
	otherWindow.setAttribute(Qt::WA_DontShowOnScreen);
	HistoryView otherView(otherHistory, &otherWindow);
	otherHistory.load();
	otherRepository.finishLoad({textEntry("")});
	require(otherHistory.entries().isEmpty() && otherView.findChild<QListWidget *>()->count() == 0,
		"Undisplayable persisted entry was retained");
}

void syncedSelection()
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	HistoryView view(history, &window);
	history.load(); repository.finishLoad({textEntry("selected item", QDateTime::currentDateTime().addSecs(-10))});
	auto *list = view.findChild<QListWidget *>();
	list->setCurrentRow(0);
	auto *selected = list->currentItem();
	auto *search = view.findChild<LineEdit *>();
	search->setText("selected");
	history.mergeSynced(textEntry("remote item"), {});
	require(list->currentItem() == selected, "Remote arrival stole the current selection");
	require(list->item(0)->isHidden(), "Remote arrival bypassed the active filter");
}

void searchNavigation(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.resize(1200, 450);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(&view);
	history.load();
	repository.finishLoad({textEntry("hidden before"), textEntry("match first"),
		textEntry("hidden between"), textEntry("match last")});
	auto *list = view.findChild<QListWidget *>();
	auto *search = view.findChild<LineEdit *>();
	window.show(); window.activateWindow();
	search->setFocus();
	waitUntil([&] { return search->hasFocus(); });
	search->setText("match");
	require(list->currentRow() == 1 && search->hasFocus(), "Search did not preselect its first match");
	auto press = [&](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
		QWidget *focused = QApplication::focusWidget();
		require(focused != nullptr, "Navigation fixture lost focus");
		QKeyEvent event(QEvent::KeyPress, key, modifiers);
		QCoreApplication::sendEvent(focused, &event);
	};
	auto selected = [&](int row) {
		return list->currentRow() == row && list->itemWidget(list->item(row))->hasFocus();
	};
	press(Qt::Key_Tab);
	require(selected(3), "First Tab from search did not advance past the selected result");
	press(Qt::Key_Right);
	require(selected(3), "Right arrow wrapped past the final search result");
	search->setFocus(); press(Qt::Key_Tab);
	require(selected(1), "Tab from search did not wrap from the last visible result");
	press(Qt::Key_Left);
	require(selected(1), "Left arrow wrapped before the first search result");
	search->setFocus(); press(Qt::Key_Backtab, Qt::ShiftModifier);
	require(selected(3), "Shift-Tab from search did not wrap backwards");
	press(Qt::Key_Backtab, Qt::ShiftModifier);
	require(selected(1), "Shift-Tab on a card did not skip hidden results");
	press(Qt::Key_Tab); press(Qt::Key_Tab);
	require(selected(1), "Tab stopped cycling after entering the cards");

	/* Preserve the search arrow's existing focus-transfer behavior. */
	search->setFocus(); press(Qt::Key_Right);
	require(selected(1), "Right arrow from search stopped entering the current result");
	search->setFocus(); search->setText("match last"); press(Qt::Key_Tab);
	require(selected(3), "Tab failed with a single search result");
	search->setFocus(); search->setText("no matches");
	press(Qt::Key_Tab); press(Qt::Key_Backtab, Qt::ShiftModifier);
	require(search->hasFocus(), "Tab left search when there were no visible results");

	search->clear(); list->setCurrentRow(1); search->setFocus(); press(Qt::Key_Tab);
	require(selected(2), "Tab from empty search did not advance the current selection");
	list->setCurrentRow(-1); search->setFocus(); press(Qt::Key_Tab);
	require(selected(0), "Tab without a selection did not enter the first result");
}

void searchInputMethod(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.resize(1200, 450);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(&view);
	history.load(); repository.finishLoad({textEntry("是中文"), textEntry("other")});
	auto *search = view.findChild<LineEdit *>();
	auto *list = view.findChild<QListWidget *>();
	window.show(); window.activateWindow(); view.focusCurrent();
	waitUntil([&] { return QApplication::activeWindow() == &window; });
	require(list->itemWidget(list->item(0))->hasFocus(), "Fixture did not focus its card");

	/* Cocoa checks shortcuts before asking the focused editor to compose.
	 * The first letter must already belong to search at that point. */
	QKeyEvent first(QEvent::ShortcutOverride, Qt::Key_S, Qt::NoModifier, "s");
	QCoreApplication::sendEvent(QApplication::focusWidget(), &first);
	require(search->hasFocus(), "First search key reached the input method with a card focused");
	require(search->text().isEmpty(), "Shortcut routing inserted the raw pinyin letter");
	QInputMethodEvent preedit("shi", {});
	QCoreApplication::sendEvent(search, &preedit);
	require(search->text().isEmpty() && !list->item(1)->isHidden(), "Uncommitted pinyin filtered history");
	QInputMethodEvent commit;
	commit.setCommitString(QString::fromUtf8("是"));
	QCoreApplication::sendEvent(search, &commit);
	require(search->text() == QString::fromUtf8("是"), "Committed Chinese retained raw pinyin");
	require(!list->item(0)->isHidden() && list->item(1)->isHidden(), "Committed Chinese did not filter history");

	search->clear(); view.focusCurrent();
	QKeyEvent latinStart(QEvent::ShortcutOverride, Qt::Key_O, Qt::NoModifier, "o");
	QCoreApplication::sendEvent(QApplication::focusWidget(), &latinStart);
	QKeyEvent latin(QEvent::KeyPress, Qt::Key_O, Qt::NoModifier, "o");
	QCoreApplication::sendEvent(QApplication::focusWidget(), &latin);
	require(search->text() == "o", "Direct Latin typing was lost or duplicated");
}

void composingSearchCommands(void)
{
	QWidget window;
	QVBoxLayout layout(&window);
	SearchBar bar(&window, 360, 38);
	layout.addWidget(&bar);
	auto *search = bar.findChild<LineEdit *>();
	int pasted = 0, moved = 0, hidden = 0;
	QObject::connect(&bar, &SearchBar::selectItem, [&] { ++pasted; });
	QObject::connect(&bar, &SearchBar::selectPlainTextItem, [&] { ++pasted; });
	QObject::connect(&bar, &SearchBar::moveFocusPrevNext, [&](bool, bool) { ++moved; });
	QObject::connect(&bar, &SearchBar::hideWindow, [&] { ++hidden; });
	window.show(); window.activateWindow(); search->setFocus();
	waitUntil([&] { return search->hasFocus(); });
	for (int key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Escape, Qt::Key_Tab,
		Qt::Key_Backtab, Qt::Key_Right, Qt::Key_Down, Qt::Key_Space}) {
		QInputMethodEvent preedit("shi", {});
		QCoreApplication::sendEvent(search, &preedit);
		QKeyEvent shortcut(QEvent::ShortcutOverride, key, Qt::ShiftModifier);
		shortcut.ignore();
		QCoreApplication::sendEvent(search, &shortcut);
		require(shortcut.isAccepted(), "Composition leaked a shortcut to the panel");
		QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
		QCoreApplication::sendEvent(search, &press);
		require(!pasted && !moved && !hidden, "Composition triggered a panel command");
	}
	QInputMethodEvent cancel;
	QCoreApplication::sendEvent(search, &cancel);
	search->clear();
	QInputMethodEvent partial("wen", {});
	partial.setCommitString(QString::fromUtf8("中"));
	QCoreApplication::sendEvent(search, &partial);
	require(search->text() == QString::fromUtf8("中"), "Partial commit saved its remaining pinyin");
	QKeyEvent composingEnter(QEvent::KeyPress, Qt::Key_Return, Qt::ShiftModifier);
	QCoreApplication::sendEvent(search, &composingEnter);
	require(!pasted, "Partial commit ended composition before its remaining candidates");
	search->actions().first()->trigger();
	require(search->text().isEmpty(), "Clearing search retained input method text");
	QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QCoreApplication::sendEvent(search, &enter);
	QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QCoreApplication::sendEvent(search, &tab);
	require(pasted == 1 && moved == 1, "Panel commands did not resume after composition");
}

void searchKeepsInputFocus(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.resize(1200, 450);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(&view);
	history.load();
	repository.finishLoad({textEntry("other"), textEntry("收到 sd")});
	auto *search = view.findChild<LineEdit *>();
	auto *list = view.findChild<QListWidget *>();
	window.show(); window.activateWindow(); search->setFocus();
	waitUntil([&] { return search->hasFocus(); });
	int focusLosses = 0;
	QObject observer;
	QObject::connect(qApp, &QApplication::focusChanged, &observer,
		[&](QWidget *before, QWidget *) { if (before == search) ++focusLosses; });

	/* The first match must change the current row. Merely restoring search
	 * focus afterward commits Cocoa's still-active preedit a second time. */
	QInputMethodEvent preedit("s'd", {});
	QCoreApplication::sendEvent(search, &preedit);
	QInputMethodEvent commit;
	commit.setCommitString("sd");
	QCoreApplication::sendEvent(search, &commit);
	require(search->text() == "sd" && list->currentRow() == 1,
		"Latin IME confirmation did not select the matching result");
	require(focusLosses == 0, "Search filtering temporarily stole input focus");
	search->clear();
	require(list->currentRow() == 0 && focusLosses == 0,
		"Restoring the previous selection stole search focus");

	QInputMethodEvent partial("dao", {});
	partial.setCommitString(QString::fromUtf8("收"));
	QCoreApplication::sendEvent(search, &partial);
	require(search->text() == QString::fromUtf8("收") && list->currentRow() == 1 && focusLosses == 0,
		"Filtering a partial Chinese commit interrupted composition");
	commit.setCommitString(QString::fromUtf8("到"));
	QCoreApplication::sendEvent(search, &commit);
	require(search->text() == QString::fromUtf8("收到") && focusLosses == 0,
		"Chinese confirmation lost search focus");
	search->setText("no match");
	require(list->item(0)->isHidden() && list->item(1)->isHidden() && search->hasFocus() && focusLosses == 0,
		"An empty result set stole search focus");
}

void pointerCommands(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.setAttribute(Qt::WA_DontShowOnScreen);
	window.resize(1200, 360);
	auto *view = new HistoryView(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(view);
	history.load();
	repository.finishLoad({textEntry("drag first"), textEntry("drag second")});
	auto *list = view->findChild<QListWidget *>();
	auto *pointer = view->findChild<CardInteractionController *>();
	auto *swipe = window.findChild<CardSwipeOverlay *>();
	for (QWidget *child : window.findChildren<QWidget *>())
		if (child->isWindow()) child->setAttribute(Qt::WA_DontShowOnScreen);
	window.show();
	list->doItemsLayout();
	auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->item(0)));
	require(pointer && swipe && card && card->isVisible(), "Gesture fixture is not laid out");
	const QPoint origin = card->mapToGlobal(card->rect().center());
	auto mouse = [&](QEvent::Type type, const QPoint &delta) {
		const QPoint global = origin+delta;
		const bool move = type == QEvent::MouseMove;
		QMouseEvent event(type, card->mapFromGlobal(global), global,
			move ? Qt::NoButton : Qt::LeftButton,
			type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
			Qt::NoModifier);
		require(pointer->handleEvent(card, &event), "Gesture was not handled");
	};
	/* Horizontal direction must stay locked even after moving upward. */
	mouse(QEvent::MouseButtonPress, {});
	mouse(QEvent::MouseMove, {-50, -2});
	mouse(QEvent::MouseMove, {-50, -260});
	mouse(QEvent::MouseButtonRelease, {-50, -260});
	require(history.entries().size() == 2 && !history.canUndo(), "Browsing turned into deletion");
	/* The final release position can cancel an otherwise ready deletion. */
	mouse(QEvent::MouseButtonPress, {});
	mouse(QEvent::MouseMove, {0, -260});
	require(swipe->ready(), "Upward drag did not reach dismissal threshold");
	mouse(QEvent::MouseButtonRelease, {0, -20});
	require(history.entries().size() == 2 && !history.canUndo(), "Below-threshold release deleted a card");
	view->cancelInteractions();
	mouse(QEvent::MouseButtonPress, {});
	mouse(QEvent::MouseMove, {0, -260});
	mouse(QEvent::MouseButtonRelease, {0, -260});
	require(history.entries().size() == 1 && list->count() == 1, "Dismissal was not committed at release");
	require(swipe->dismissalId(card) != 0, "Dismissal lost its visual identity");
	for (QShortcut *shortcut : view->findChildren<QShortcut *>()) {
		if (shortcut->key() == QKeySequence("Ctrl+Z"))
			QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection);
	}
	require(list->count() == 2 && history.entries().first()->mimeData->text() == "drag first",
		"Immediate undo lost the original position");
	require(swipe->sourceCard() && swipe->offset() > 200,
		"Immediate undo did not continue from the live drag position");
	view->cancelInteractions();
	require(swipe->sourceCard() == nullptr, "Cancelling interactions retained the restored card");
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	int failures = runTest("history view commands, filtering and ownership", viewCommands);
	failures += runTest("pointer direction, cancellation and immediate undo", pointerCommands);
	failures += runTest("synced items preserve selection and search", syncedSelection);
	failures += runTest("search Tab advances the selected result and cycles", searchNavigation);
	failures += runTest("type-to-search focuses before input method composition", searchInputMethod);
	failures += runTest("search composition keeps candidate keys", composingSearchCommands);
	failures += runTest("search filtering preserves uninterrupted input focus", searchKeepsInputFocus);
	return failures ? 1 : 0;
}
