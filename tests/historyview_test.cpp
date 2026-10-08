#include "tests/testsupport.h"
#include "ui/historyview.h"
#include "ui/pasteitem.h"
#include "ui/searchbar.h"
#include "ui/cardinteraction.h"
#include "ui/cardswipe.h"
#include "ui/elasticscroll.h"
#include "ui/previewdialog.h"
#include "ui/sourceiconview.h"
#include "ui/filepreview.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QMouseEvent>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTemporaryDir>
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


void favoritesFilter(void)
{
	MemoryRepository repository; HistoryService history(repository);
	QWidget window; window.resize(1200, 450);
	HistoryView view(history, &window);
	auto ordinary = textEntry("ordinary"), alpha = textEntry("saved alpha"), beta = textEntry("saved beta");
	history.load(); repository.finishLoad({ordinary, alpha, beta});
	auto *list = view.findChild<QListWidget *>();
	auto *favorites = view.findChild<QPushButton *>("FavoritesTab");
	auto *all = view.findChild<QPushButton *>("HistoryTab");
	auto card = [&](int row) { return qobject_cast<PasteItem *>(list->itemWidget(list->item(row))); };
	auto *star = card(1)->findChild<QPushButton *>("FavoriteButton");
	require(star->isHidden(), "History displayed an ordinary card star");
	card(1)->favoriteRequested(); history.setFavorite(beta->id, true);
	require(alpha->favorite && star->isChecked() && star->isHidden(),
		"Favorite command lost its state or displayed a star in history");
	history.setFavoriteName(alpha->id, "<b>常用命令</b>");
	favorites->click();
	require(!star->isHidden(), "Favorites hid the card star");
	require(card(0)->entry() == alpha && card(1)->entry() == beta && list->item(2)->isHidden() && list->currentRow() == 0,
		"Favorites did not use fixed order and select the first visible card");
	auto *name = card(0)->findChild<QLabel *>("CardType");
	require(name->textFormat() == Qt::PlainText && name->accessibleName() == alpha->favoriteDetails.name &&
		name->toolTip().contains(alpha->favoriteDetails.name.toHtmlEscaped()) &&
		!card(0)->findChild<QLabel *>("FavoriteName"),
		"Favorite title interpreted markup, lost its full name or added another content row");
	HistoryEntry renamed;
	QObject::connect(&view, &HistoryView::renameFavoriteRequested, &view, [&](HistoryEntry entry) { renamed = entry; });
	QKeyEvent rename(QEvent::KeyPress, Qt::Key_F2, Qt::NoModifier);
	QCoreApplication::sendEvent(card(0), &rename);
	require(renamed == alpha, "Favorite rename shortcut lost the typed entry");
	auto *search = view.findChild<LineEdit *>(); search->setText("常用");
	require(!list->item(0)->isHidden() && list->item(1)->isHidden(), "Favorite name was not searchable");
	search->setText("beta");
	require(list->currentRow() == 1 && list->item(0)->isHidden(), "Search bypassed the favorites filter");
	HistoryEntry copied;
	QObject::connect(&view, &HistoryView::copyRequested, &view, [&](HistoryEntry entry, bool, bool) { copied = entry; });
	for (QShortcut *shortcut : view.findChildren<QShortcut *>())
		if (shortcut->key() == QKeySequence("Ctrl+1")) QMetaObject::invokeMethod(shortcut, "activated");
	require(copied == beta, "Quick paste numbering included hidden cards");
	search->clear();
	list->setCurrentRow(0); auto *selected = list->currentItem(); auto *selectedCard = card(0);
	history.moveFavorite(alpha->id, beta->id);
	require(card(1) == selectedCard && list->currentItem() == selected && card(0)->entry() == beta,
		"Sorting destroyed a card widget or selected another item");
	history.record(textEntry("saved alpha"));
	const auto recopy = history.entries().first();
	require(card(1)->entry() == recopy && recopy->favorite && !list->item(1)->isHidden(),
		"Re-copy reset the fixed favorite position");
	history.record(textEntry("new ordinary"));
	require(card(0)->entry() == beta && list->currentItem() == card(1)->widgetItem(),
		"Ordinary capture bypassed favorites or stole selection");
	all->click();
	for (int row = 0; row < list->count(); ++row)
		require(!list->item(row)->isHidden() && card(row)->entry() == history.entries()[row] &&
			card(row)->findChild<QPushButton *>("FavoriteButton")->isHidden(),
			"History tab lost its chronological order or displayed a favorite star");
	favorites->click();
	card(0)->findChild<QPushButton *>("FavoriteButton")->click();
	require(list->currentItem() == card(0)->widgetItem() && card(0)->entry() == recopy,
		"Unstarring retained an invisible selection");
	card(0)->findChild<QPushButton *>("FavoriteButton")->click();
	require(list->currentRow() == -1 && view.findChild<QLabel *>("EmptyState")->text().contains("Right-click"),
		"Empty favorites retained a hidden selection");
}

void longTextCards(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	HistoryView view(history, &window);
	const QString fullText = QString(1500000, 'x') + " searchable tail";
	auto plain = textEntry(fullText);
	auto rich = textEntry(fullText);
	rich->mimeData->setHtml("<p>" + fullText + "</p>");
	auto markup = textEntry("<b>literal clipboard text</b>");
	auto formatted = textEntry("formatted");
	formatted->mimeData->setHtml("<b>formatted</b>");
	auto medium = textEntry(QString(2000, 'm'));
	auto lines = textEntry(QString("short line\n").repeated(50) + "last line");
	auto unicode = textEntry(QString(255, 'u') + QString::fromUtf8("😀") + QString(100, 'v'));
	int countChanges = 0;
	QObject::connect(&view, &HistoryView::countChanged, &view, [&] { ++countChanges; });
	history.load();
	repository.finishLoad({plain, rich, markup, formatted, medium, lines, unicode});
	auto *list = view.findChild<QListWidget *>();
	require(list->count() == 7 && countChanges == 1, "History load did not finalize its card batch once");
	for (int row : {0, 1}) {
		auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->item(row)));
		auto *label = card->findChild<QLabel *>("ContextTextFrame");
		require(label && label->text().size() <= 257 && label->text().endsWith(QChar(0x2026)),
			"Long card text was not reduced to an excerpt");
		require(label->textFormat() == Qt::PlainText && card->text() == fullText,
			"Card excerpt replaced the searchable content");
		require(card->entry()->mimeData->text() == fullText, "Card excerpt truncated clipboard content");
		require(label->findChild<QLabel *>()->text().startsWith(QString::number(fullText.size())),
			"Card footer reported the excerpt length instead of the full length");
	}
	auto *literal = list->itemWidget(list->item(2))->findChild<QLabel *>("ContextTextFrame");
	auto *styled = list->itemWidget(list->item(3))->findChild<QLabel *>("ContextTextFrame");
	require(literal->textFormat() == Qt::PlainText && literal->text() == markup->mimeData->text(),
		"Plain clipboard markup was interpreted as HTML");
	require(styled->textFormat() == Qt::RichText && styled->text() == formatted->mimeData->html(),
		"Ordinary rich-text formatting was lost");
	auto *mediumLabel = list->itemWidget(list->item(4))->findChild<QLabel *>("ContextTextFrame");
	require(mediumLabel->text().size() <= 257 && mediumLabel->text().endsWith(QChar(0x2026)),
		"A medium-size record still filled the card with thousands of characters");
	auto *lineLabel = list->itemWidget(list->item(5))->findChild<QLabel *>("ContextTextFrame");
	require(lineLabel->text().count('\n') < 12 && lineLabel->text().endsWith(QChar(0x2026)),
		"Short lines exceeded the card excerpt's line budget");
	auto *unicodeLabel = list->itemWidget(list->item(6))->findChild<QLabel *>("ContextTextFrame");
	require(QString::fromUtf8(unicodeLabel->text().toUtf8()) == unicodeLabel->text(),
		"Card excerpt split a Unicode surrogate pair");
	view.findChild<LineEdit *>()->setText("searchable tail");
	require(!list->item(0)->isHidden() && !list->item(1)->isHidden() && list->item(2)->isHidden(),
		"Search could not find text beyond the card excerpt");
	HistoryEntry copied;
	QObject::connect(&view, &HistoryView::copyRequested, &view,
		[&](HistoryEntry entry, bool, bool) { copied = entry; });
	QKeyEvent copy(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QCoreApplication::sendEvent(list->itemWidget(list->item(1)), &copy);
	require(copied == rich && copied->mimeData->html() == rich->mimeData->html(),
		"Copy command lost the original rich content");
	PreviewDialog preview(*plain, &window);
	require(preview.findChild<QPlainTextEdit *>("PreviewText")->toPlainText() == fullText,
		"Explicit preview was truncated with the card excerpt");
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

void searchNavigationGeometry(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.resize(1200, 450);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(&view);
	window.setStyleSheet("QListWidget { border: 0; }");
	history.load();
	repository.finishLoad({textEntry("match first"), textEntry("match second"), textEntry("other")});
	auto *search = view.findChild<LineEdit *>();
	auto *list = view.findChild<QListWidget *>();
	auto *scroll = view.findChild<ElasticScrollController *>();
	window.show(); window.activateWindow(); search->setFocus();
	waitUntil([&] { return search->hasFocus(); });
	list->doItemsLayout();
	const QPoint origin = list->viewport()->pos();
	const int cardTop = list->itemWidget(list->item(0))->mapTo(&window, QPoint()).y();
	auto pressTab = [&] {
		QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
		QCoreApplication::sendEvent(QApplication::focusWidget(), &tab);
		require(list->viewport()->pos() == origin, "Tab moved the idle viewport");
		require(list->itemWidget(list->item(0))->mapTo(&window, QPoint()).y() == cardTop,
			"Tab shifted the row of cards vertically");
	};
	pressTab();
	require(list->currentRow() == 1 && list->itemWidget(list->item(1))->hasFocus(),
		"Tab no longer advances from search to the next card");
	search->setFocus(); search->setText("match");
	pressTab();
	require(list->currentRow() == 1, "Filtered Tab stopped advancing selection");

	/* Cancelling real overshoot must still return to the settled layout. */
	scroll->beginDrag(); scroll->dragTo(80);
	require(list->viewport()->x() > origin.x(), "Fixture did not stretch the viewport");
	search->setFocus(); pressTab();
	require(!scroll->active() && list->currentRow() == 0,
		"Tab failed to cancel overshoot and cycle selection");
	scroll->wheel(-80, true, Qt::ScrollBegin);
	require(list->viewport()->x() > origin.x(), "Fixture did not stretch with a touchpad");
	scroll->cancel();
	require(list->viewport()->pos() == origin, "Touchpad cancellation did not restore the viewport");
}

void previewKeyboardRouting(void)
{
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	window.resize(1200, 450);
	HistoryView view(history, &window);
	QVBoxLayout layout(&window);
	layout.addWidget(&view);
	history.load();
	auto first = textEntry("first preview"), second = textEntry("second preview");
	repository.finishLoad({first, second});
	auto *list = view.findChild<QListWidget *>();
	auto *search = view.findChild<LineEdit *>();
	window.show(); window.activateWindow(); view.focusCurrent();
	waitUntil([&] { return QApplication::activeWindow() == &window; });
	list->setCurrentRow(1);
	HistoryEntry previewed;
	int requests = 0;
	QObject::connect(&view, &HistoryView::previewRequested, &view,
		[&](HistoryEntry entry) { previewed = entry; ++requests; });
	auto press = [](QWidget *receiver, bool repeat = false) {
		QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", repeat);
		QCoreApplication::sendEvent(receiver, &space);
	};
	QWidget *card = list->itemWidget(list->item(1));
	for (QWidget *receiver : {static_cast<QWidget *>(&view), list->viewport(), &window, card}) {
		const int before = requests;
		receiver->setFocus();
		press(receiver);
		require(requests == before+1 && previewed == second,
			"Space did not preview the selected entry outside card focus");
		press(receiver, true);
		require(requests == before+1, "Holding Space repeated the preview command");
	}
	search->setFocus();
	search->setText("second");
	const int before = requests;
	press(search);
	require(requests == before && search->text() == "second ", "Preview consumed a search space");
	QInputMethodEvent preedit("shi", {});
	QCoreApplication::sendEvent(search, &preedit);
	press(search);
	require(requests == before && search->hasFocus(), "Preview interrupted candidate selection");
	QInputMethodEvent cancel;
	QCoreApplication::sendEvent(search, &cancel);
	search->setText("no matching entry");
	view.setFocus();
	press(&view);
	require(requests == before, "Space previewed a hidden result");
	search->clear();
	PreviewDialog dialog(*first, &window);
	dialog.show(); dialog.activateWindow();
	waitUntil([&] { return QApplication::activeWindow() == &dialog; });
	press(&view);
	require(requests == before, "Space opened history behind a modal preview");
	dialog.hide(); window.hide();
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

void imageFilePreviews(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath(QString::fromUtf8("预览 image.png"));
	QImage pixels(64, 48, QImage::Format_RGB32);
	pixels.fill(Qt::cyan);
	require(pixels.save(path), "Cannot save preview image fixture");
	auto file = textEntry(path);
	file->mimeData->setUrls({QUrl::fromLocalFile(path)});
	file->md5 = ClipboardContent::fingerprint(*file->mimeData);
	auto checkPreview = [](const HistoryEntry &entry, bool image, const QString &fallback = QString()) {
		const QByteArray fingerprint = ClipboardContent::fingerprint(*entry->mimeData);
		PreviewDialog dialog(*entry);
		const auto *text = dialog.findChild<QPlainTextEdit *>("PreviewText");
		bool picture = false;
		for (const QWidget *child : dialog.findChildren<QWidget *>())
			picture |= child->accessibleName() == QObject::tr("Image preview");
		require(picture == image && bool(text) != image, "Preview chose the wrong content representation");
		if (text) require(text->toPlainText() == fallback, "Preview lost its original path fallback");
		require(ClipboardContent::fingerprint(*entry->mimeData) == fingerprint,
			"Preview changed the clipboard payload");
	};
	checkPreview(file, true);
	checkPreview(textEntry(path), true);
	checkPreview(textEntry('"'+path+'"'), true);
	checkPreview(textEntry(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)), true);
	auto rich = textEntry(path);
	rich->mimeData->setHtml("<p>"+path+"</p>");
	checkPreview(rich, true);
	auto bitmap = textEntry("");
	bitmap->mimeData->setImageData(pixels);
	checkPreview(bitmap, true);

	/* Opening a card must preserve a file payload, including its commands. */
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	HistoryView view(history, &window);
	history.load(); repository.finishLoad({file});
	auto *list = view.findChild<QListWidget *>();
	require(list->count() == 1, "Image file card did not load");
	auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->item(0)));
	HistoryEntry previewed, copied;
	QObject::connect(&view, &HistoryView::previewRequested, &view,
		[&](HistoryEntry entry) { previewed = entry; });
	QObject::connect(&view, &HistoryView::copyRequested, &view,
		[&](HistoryEntry entry, bool, bool) { copied = entry; });
	QKeyEvent space(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
	QCoreApplication::sendEvent(card, &space);
	card->copyData();
	require(previewed == file && copied == file && file->mimeData->urls() == QList<QUrl>{QUrl::fromLocalFile(path)},
		"Preview or copy replaced the file entry with decoded pixels");

	/* File-only previews resolve the source afresh, while separately supplied
	 * clipboard pixels remain self-contained after the file disappears. */
	require(QFile::remove(path), "Cannot remove preview source fixture");
	checkPreview(file, false, path);
	checkPreview(textEntry(path), false, path);
	auto suppliedBitmap = cloneEntry(*file);
	suppliedBitmap->mimeData->setImageData(pixels);
	checkPreview(suppliedBitmap, true);
	QFile ordinary(path);
	require(ordinary.open(QIODevice::WriteOnly), "Cannot create unreadable image fixture");
	ordinary.write("ordinary file contents"); ordinary.close();
	checkPreview(file, false, path);
	require(pixels.save(path), "Cannot restore image fixture");
	checkPreview(file, true);
	auto mixed = cloneEntry(*file);
	mixed->mimeData->setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(directory.path())});
	checkPreview(mixed, false, path+'\n'+directory.path());
	checkPreview(textEntry(directory.path()), false, directory.path());
	checkPreview(textEntry("https://example.invalid/photo.png"), false, "https://example.invalid/photo.png");
	const QString pdfPath = directory.filePath("document.pdf");
	{
		QPdfWriter pdf(pdfPath);
		QPainter painter(&pdf);
		painter.drawText(100, 100, "Ordinary document");
	}
	checkPreview(textEntry(pdfPath), false, pdfPath);

	/* Reloading history after removal must retain the path and not delete
	 * the persisted record merely because its source file is unavailable. */
	require(QFile::remove(path), "Cannot remove image fixture before reload");
	MemoryRepository reloadRepository;
	HistoryService reloadHistory(reloadRepository);
	QWidget reloadWindow;
	HistoryView reloadView(reloadHistory, &reloadWindow);
	reloadHistory.load(); reloadRepository.finishLoad({file});
	require(reloadHistory.entries().size() == 1 && reloadView.findChild<QListWidget *>()->count() == 1 &&
		reloadRepository.removals.isEmpty(), "Missing source file deleted its history record");
}

void svgFilePreviews(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath(QString::fromUtf8("矢量 preview.svg"));
	const QUrl url = QUrl::fromLocalFile(path);
	const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"8\" viewBox=\"0 0 16 8\">"
		"<rect width=\"16\" height=\"8\" fill=\"COLOR\"/>"
		"<circle cx=\"4\" cy=\"4\" r=\"2\" fill=\"#287E68\"/>"
		"<path d=\"M10 2h4M10 4h4M10 6h2\" stroke=\"white\" stroke-width=\"0.6\"/></svg>";
	auto writeSvg = [&](const char *color) {
		QFile file(path);
		require(file.open(QIODevice::WriteOnly), "Cannot write SVG fixture");
		const QByteArray contents = QByteArray(svg).replace("COLOR", color);
		require(file.write(contents) == contents.size(), "Incomplete SVG fixture");
	};
	writeSvg("#111111");
	const QImage large = FilePreview::loadImage(url, 320);
	require(!large.isNull(), "SVG image plugin cannot render the local file");
	require(large.size() == QSize(320, 160), "Small SVG was not rasterized at the requested resolution");
	auto entry = textEntry(path);
	entry->mimeData->setUrls({url});
	QImage fileIcon(8, 8, QImage::Format_RGB32); fileIcon.fill(Qt::green);
	entry->mimeData->setImageData(fileIcon);
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	const QByteArray fingerprint = entry->md5;
	PasteItem card;
	card.setAttribute(Qt::WA_DontShowOnScreen);
	card.resize(280, 260);
	require(card.setEntry(entry, true), "SVG file card did not load");
	/* The file changes after card construction: the first paint must read
	 * the visible source, not eagerly decode it during history loading. */
	writeSvg("#E06F20");
	card.show();
	card.grab();
	auto *thumbnail = card.findChild<QLabel *>("SvgFilePreview");
	require(thumbnail && !thumbnail->pixmap().isNull(), "SVG card retained a generic file icon");
	auto centerColor = [&] {
		const QImage image = thumbnail->pixmap().toImage();
		return image.pixelColor(image.width()/2, image.height()/2);
	};
	require(centerColor() == QColor("#E06F20"), "SVG card did not draw the visible source artwork");
	writeSvg("#135BDA");
	card.grab();
	require(centerColor() == QColor("#E06F20"), "Repeated SVG paints reread the source instead of the cache");
	const int oldSize = thumbnail->width();
	card.resize(280, 360);
	card.grab();
	require(thumbnail->width() != oldSize && centerColor() == QColor("#135BDA"),
		"SVG thumbnail did not rerender for a changed display size");
	HistoryEntry copied;
	QObject::connect(&card, &PasteItem::copyRequested, &card,
		[&](HistoryEntry value, bool, bool) { copied = value; });
	card.copyData();
	require(copied == entry && entry->mimeData->urls() == QList<QUrl>{url} &&
		ClipboardContent::fingerprint(*entry->mimeData) == fingerprint &&
		qvariant_cast<QImage>(entry->mimeData->imageData()) == fileIcon,
		"SVG display replaced the original file payload or persisted identity");
	PreviewDialog preview(*entry);
	require(!preview.findChild<QPlainTextEdit *>("PreviewText"), "SVG preview fell back to its file path");
	const QString renderDirectory = qApp->arguments().value(qApp->arguments().indexOf("--render-dir")+1);
	if (qApp->arguments().contains("--render-dir")) {
		require(card.grab().save(renderDirectory+"/svg-file-card.png"), "Cannot save SVG card preview");
		preview.show();
		require(preview.grab().save(renderDirectory+"/svg-file-preview.png"), "Cannot save SVG dialog preview");
		preview.hide();
	}
	require(QFile::remove(path), "Cannot remove SVG fixture");
	PreviewDialog missing(*entry);
	auto *text = missing.findChild<QPlainTextEdit *>("PreviewText");
	require(text && text->toPlainText() == path, "Missing SVG file lost its path fallback");
	QFile invalid(path);
	require(invalid.open(QIODevice::WriteOnly), "Cannot create invalid SVG fixture");
	invalid.write("<svg invalid"); invalid.close();
	require(FilePreview::loadImage(url, 320).isNull(), "Invalid SVG created a misleading image preview");
}

void svgColorPreviews(void)
{
	QFile fixture(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()+"/fixtures/pattern-hsla.svg");
	require(fixture.open(QIODevice::ReadOnly), "Cannot open the reported SVG pattern fixture");
	const QByteArray original = fixture.readAll();
	QTemporaryDir directory;
	const QString path = directory.filePath("pattern.svg");
	auto writeSvg = [&](const QByteArray &bytes) {
		QFile file(path);
		require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write color SVG");
	};
	writeSvg(original);
	const QUrl url = QUrl::fromLocalFile(path);
	const QImage image = FilePreview::loadImage(url, 320).convertToFormat(QImage::Format_ARGB32);
	require(image.size() == QSize(320, 320), "Reported SVG did not render at the requested size");
	require(image.pixelColor(160, 160) == QColor(Qt::white), "SVG HSLA background rendered as black");
	auto containsColor = [](const QImage &pixels, const QColor &expected) {
		int count = 0;
		for (int y = 0; y < pixels.height(); ++y) {
			const auto *row = reinterpret_cast<const QRgb *>(pixels.constScanLine(y));
			for (int x = 0; x < pixels.width(); ++x)
				if (qAbs(qRed(row[x])-expected.red()) <= 2 && qAbs(qGreen(row[x])-expected.green()) <= 2 &&
					qAbs(qBlue(row[x])-expected.blue()) <= 2 && qAlpha(row[x]) == 255) ++count;
		}
		return count > 100;
	};
	for (const QColor color : {QColor(128, 90, 213), QColor(233, 30, 99), QColor(3, 169, 244), QColor(236, 201, 75)})
		require(containsColor(image, color), "SVG lost one of the four HSLA wave colors");
	auto entry = textEntry(path);
	entry->mimeData->setUrls({url});
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	const QByteArray fingerprint = entry->md5;
	PasteItem card;
	card.setAttribute(Qt::WA_DontShowOnScreen);
	card.resize(280, 360);
	require(card.setEntry(entry, true), "Reported SVG card did not load");
	card.show(); card.grab();
	auto *thumbnail = card.findChild<QLabel *>("SvgFilePreview");
	require(thumbnail && thumbnail->pixmap().toImage().pixelColor(thumbnail->pixmap().width()/2,
		thumbnail->pixmap().height()/2) == QColor(Qt::white), "SVG card still shows a black tile");
	PreviewDialog preview(*entry);
	require(!preview.findChild<QPlainTextEdit *>("PreviewText"), "Reported SVG preview fell back to text");
	if (qApp->arguments().contains("--render-dir")) {
		const QString output = qApp->arguments().value(qApp->arguments().indexOf("--render-dir")+1);
		require(card.grab().save(output+"/pattern-card.png"), "Cannot save the reported SVG card");
		preview.setAttribute(Qt::WA_DontShowOnScreen);
		preview.show();
		require(preview.grab().save(output+"/pattern-preview.png"), "Cannot save the reported SVG preview");
		preview.hide();
		require(image.save(output+"/pattern-after.png"), "Cannot save the rendered pattern");
	}
	QFile file(path);
	require(file.open(QIODevice::ReadOnly) && file.readAll() == original &&
		ClipboardContent::fingerprint(*entry->mimeData) == fingerprint,
		"SVG color compatibility changed the original file or clipboard identity");
	file.close();
	writeSvg("<svg xmlns='http://www.w3.org/2000/svg' width='12' height='4'>"
		"<style><![CDATA[.green {fill: hsl(120,100%,50%)}]]></style>"
		"<rect width='4' height='4' class='green'/>"
		"<g fill-opacity='0.5'><rect x='4' width='4' height='4' fill='hsla(0,100%,50%,0.5)'/></g>"
		"<rect x='8' width='4' height='4' style='fill: hsl(240deg 100% 50% / 25%);fill-opacity:0.5'/>"
		"</svg>");
	const QImage colors = FilePreview::loadImage(url, 120);
	require(colors.pixelColor(20, 20) == QColor(Qt::green), "SVG stylesheet HSL color failed");
	const QColor red = colors.pixelColor(60, 20), blue = colors.pixelColor(100, 20);
	require(red.red() == 255 && qAbs(red.alpha()-64) <= 1,
		"HSLA alpha did not combine with inherited fill opacity");
	require(blue.blue() == 255 && qAbs(blue.alpha()-32) <= 1,
		"Inline modern HSL alpha did not combine with explicit fill opacity");
	QImage embedded(4, 4, QImage::Format_RGB32); embedded.fill(Qt::cyan);
	require(embedded.save(directory.filePath(QString::fromUtf8("图片 file.png"))), "Cannot write relative SVG image");
	writeSvg(QString::fromUtf8("<svg xmlns='http://www.w3.org/2000/svg' xmlns:s='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='12' height='4'>"
		"<s:defs><s:rect id='tile' width='4' height='4'/></s:defs>"
		"<s:g fill='hsl(-240,100%,50%)'><s:use xlink:href='#tile'/></s:g>"
		"<s:image x='4' width='4' height='4' xlink:href='图片 file.png'/>"
		"<s:g stroke-opacity='0.5'><s:path d='M8 2h4' stroke='hsla(0.5turn 100% 50% / 0.5)' stroke-width='1'/></s:g>"
		"</svg>").toUtf8());
	const QImage references = FilePreview::loadImage(url, 120);
	require(references.pixelColor(20, 20) == QColor(Qt::green) && references.pixelColor(60, 20) == QColor(Qt::cyan),
		"HSL normalization broke SVG namespaces, use references or relative image paths");
	const QColor stroke = references.pixelColor(100, 20);
	require(stroke.green() == 255 && stroke.blue() == 255 && qAbs(stroke.alpha()-64) <= 1,
		"Transparent HSL stroke lost its color or combined opacity");
}

void mixedImageCopies(void)
{
	QTemporaryDir directory;
	const QString path = directory.filePath("temporary picture.png");
	QImage filePixels(8, 6, QImage::Format_RGB32); filePixels.fill(Qt::red);
	require(filePixels.save(path), "Cannot prepare mixed image source");
	QImage supplied(96, 64, QImage::Format_RGB32); supplied.fill(Qt::blue);
	auto entry = textEntry("");
	entry->mimeData->setUrls({QUrl::fromLocalFile(path)});
	entry->mimeData->setImageData(supplied);
	entry->md5 = ClipboardContent::fingerprint(*entry->mimeData);
	PasteItem fresh;
	require(fresh.setEntry(entry) && fresh.findChild<QWidget *>("PasteItemFrame")->property("contentKind").toString() == "image",
		"Fresh mixed clipboard pixels were displayed as a file");
	require(fresh.text().contains(QUrl::fromLocalFile(path).toString()), "Image presentation lost its searchable file reference");
	entry->mimeData = ClipboardData::withStoredImage(entry->mimeData.get(), png(supplied), supplied.format());
	MemoryRepository repository;
	HistoryService history(repository);
	QWidget window;
	HistoryView view(history, &window);
	history.load(); repository.finishLoad({entry});
	auto *list = view.findChild<QListWidget *>();
	auto *card = qobject_cast<PasteItem *>(list->itemWidget(list->item(0)));
	require(card && card->findChild<QWidget *>("PasteItemFrame")->property("contentKind").toString() == "image",
		"Reloaded mixed clipboard pixels were displayed as a file");
	HistoryEntry copied;
	QObject::connect(&view, &HistoryView::copyRequested, &view,
		[&](HistoryEntry value, bool, bool) { copied = value; });
	card->copyData();
	require(copied == entry && entry->mimeData->urls() == QList<QUrl>{QUrl::fromLocalFile(path)} &&
		qvariant_cast<QImage>(entry->mimeData->imageData()) == supplied,
		"Image presentation changed the original mixed clipboard payload");
	auto checkPreview = [&] {
		PreviewDialog dialog(*entry);
		require(!dialog.findChild<QPlainTextEdit *>("PreviewText"), "Supplied pixels fell back to the file path");
		bool dimensions = false;
		for (const QLabel *label : dialog.findChildren<QLabel *>("PreviewMeta"))
			dimensions |= label->text() == "96 × 64 px";
		require(dimensions, "Preview read the accompanying file instead of supplied pixels");
	};
	checkPreview();
	require(QFile::remove(path), "Cannot remove temporary mixed image source");
	checkPreview();
	PasteItem missing;
	require(missing.setEntry(entry, true) && missing.findChild<QWidget *>("PasteItemFrame")->property("contentKind").toString() == "image",
		"Missing file concealed persisted clipboard pixels");
	auto files = cloneEntry(*entry);
	files->mimeData->setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(directory.path())});
	PasteItem collection;
	require(collection.setEntry(files) && collection.findChild<QWidget *>("PasteItemFrame")->property("contentKind").toString() == "file",
		"A single supplied bitmap concealed a multiple-file copy");
	auto unavailable = textEntry("");
	unavailable->mimeData->setUrls({QUrl::fromLocalFile(path)});
	unavailable->mimeData->setImageData(QImage());
	PasteItem fallback;
	require(fallback.setEntry(unavailable) && fallback.findChild<QWidget *>("PasteItemFrame")->property("contentKind").toString() == "file",
		"Unavailable clipboard pixels discarded their path fallback");
}

void sourceIconPresentation(void)
{
	QImage tight(64, 32, QImage::Format_ARGB32_Premultiplied);
	tight.fill(Qt::green);
	QImage padded(128, 128, QImage::Format_ARGB32_Premultiplied);
	padded.fill(Qt::transparent);
	{
		QPainter painter(&padded);
		painter.drawImage(20, 70, tight); // Deliberately asymmetric source padding.
	}
	auto bounds = [](const QPixmap &pixmap) {
		const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
		QRect result;
		for (int y = 0; y < image.height(); ++y) {
			const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
			for (int x = 0; x < image.width(); ++x)
				if (qAlpha(line[x]) >= 128) result |= QRect(x, y, 1, 1);
		}
		return result;
	};
	for (int logicalSize : {20, 24}) for (qreal ratio : {1.0, 1.25, 2.0, 3.0}) {
		const QPixmap first = SourceIconView::pixmap(tight, logicalSize, ratio);
		const QPixmap second = SourceIconView::pixmap(padded, logicalSize, ratio);
		const int pixels = qRound(logicalSize*ratio);
		require(first.size() == QSize(pixels, pixels) && second.size() == first.size() &&
			first.devicePixelRatio() == ratio && second.devicePixelRatio() == ratio,
			"Source icon canvas or HiDPI scale differs between platforms");
		const QRect visible = bounds(first);
		require(visible == bounds(second), "Transparent padding changed the visible icon size or center");
		if (qAbs(visible.width()-2*visible.height()) > 1 ||
			qAbs(visible.center().x()-(pixels-1)/2) > 1 || qAbs(visible.center().y()-(pixels-1)/2) > 1)
			qWarning() << "Icon bounds" << logicalSize << ratio << pixels << visible;
		require(qAbs(visible.width()-2*visible.height()) <= 1 &&
			qAbs(visible.center().x()-(pixels-1)/2) <= 1 &&
			qAbs(visible.center().y()-(pixels-1)/2) <= 1,
			"Source icon artwork was distorted or not centered");
	}
	QImage transparent(64, 64, QImage::Format_ARGB32); transparent.fill(Qt::transparent);
	require(SourceIconView::pixmap(transparent, 20, 2).isNull(), "Empty icon created visible artwork");
	/* PNG transfer can discard DPR metadata; it must not affect geometry. */
	padded.setDevicePixelRatio(2);
	require(bounds(SourceIconView::pixmap(tight, 20, 2)) == bounds(SourceIconView::pixmap(padded, 20, 2)),
		"Source DPR metadata changed the rendered icon size");
}

void sourceIconDownsampling(void)
{
	/* Detail above the display's pixel frequency must average into gray,
	 * rather than alias into black/white speckles when the icon shrinks. */
	QImage detail(128, 128, QImage::Format_ARGB32_Premultiplied);
	for (int y = 0; y < detail.height(); ++y) {
		auto *row = reinterpret_cast<QRgb *>(detail.scanLine(y));
		for (int x = 0; x < detail.width(); ++x)
			row[x] = (x+y)%2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0);
	}
	for (int size : {20, 24}) for (qreal ratio : {1.0, 1.25, 1.5, 2.0, 3.0}) {
		const QImage rendered = SourceIconView::pixmap(detail, size, ratio).toImage().convertToFormat(QImage::Format_ARGB32);
		const int pixels = qRound(size*ratio);
		require(rendered.size() == QSize(pixels, pixels), "Fine-detail source icon failed to render at its display size");
		const int margin = qCeil(rendered.width()*0.2);
		for (int y = margin; y < rendered.height()-margin; ++y) {
			const auto *row = reinterpret_cast<const QRgb *>(rendered.constScanLine(y));
			for (int x = margin; x < rendered.width()-margin; ++x)
				require(qRed(row[x]) >= 112 && qRed(row[x]) <= 144 && qAlpha(row[x]) == 255,
					"Source icon minification aliased fine detail instead of averaging its pixels");
		}
	}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	if (app.arguments().contains("--icons-only")) {
		int failures = runTest("source icons retain size and bounds", sourceIconPresentation);
		failures += runTest("source icon minification filters fine detail", sourceIconDownsampling);
		return failures ? 1 : 0;
	}
	if (app.arguments().contains("--svg-only")) {
		int failures = runTest("SVG files render lazily and preserve file copy identity", svgFilePreviews);
		failures += runTest("SVG HSL colors retain wave artwork and transparency", svgColorPreviews);
		return failures ? 1 : 0;
	}
	int failures = runTest("history view commands, filtering and ownership", viewCommands);
	failures += runTest("pointer direction, cancellation and immediate undo", pointerCommands);
	failures += runTest("favorite stars, filters, selection and quick paste", favoritesFilter);
	failures += runTest("synced items preserve selection and search", syncedSelection);
	failures += runTest("search Tab advances the selected result and cycles", searchNavigation);
	failures += runTest("search Tab keeps the card row in place", searchNavigationGeometry);
	failures += runTest("type-to-search focuses before input method composition", searchInputMethod);
	failures += runTest("Space previews selection across panel focus and preserves search input", previewKeyboardRouting);
	failures += runTest("search composition keeps candidate keys", composingSearchCommands);
	failures += runTest("search filtering preserves uninterrupted input focus", searchKeepsInputFocus);
	failures += runTest("image files preview on demand and retain missing paths", imageFilePreviews);
	failures += runTest("SVG files render lazily and preserve file copy identity", svgFilePreviews);
	failures += runTest("SVG HSL colors retain wave artwork and transparency", svgColorPreviews);
	failures += runTest("mixed image copies retain pixels independently of file paths", mixedImageCopies);
	failures += runTest("bounded card excerpts retain complete search, copy and preview", longTextCards);
	failures += runTest("source icons keep uniform visible bounds on HiDPI screens", sourceIconPresentation);
	failures += runTest("source icon minification filters fine detail", sourceIconDownsampling);
	return failures ? 1 : 0;
}
