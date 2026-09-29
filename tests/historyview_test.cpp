#include "tests/testsupport.h"
#include "ui/historyview.h"
#include "ui/pasteitem.h"
#include "ui/searchbar.h"
#include "ui/cardinteraction.h"
#include "ui/cardswipe.h"
#include <QApplication>
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
	return failures ? 1 : 0;
}
