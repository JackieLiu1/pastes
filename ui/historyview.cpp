#include "ui/historyview.h"
#include "ui/pasteitem.h"
#include "ui/searchbar.h"
#include "ui/cardswipe.h"
#include "ui/cardreflow.h"
#include "ui/cardinteraction.h"
#include "ui/elasticscroll.h"
#include "platform/windowintegration.h"
#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QSettings>
#include <QShortcut>
#include <QVBoxLayout>

namespace {
class HistoryList final : public QListWidget
{
public:
	using QListWidget::QListWidget;

protected:
	bool edit(const QModelIndex &, EditTrigger, QEvent *) override
	{
		/* Cards handle their own interaction. Qt's default editor path
		 * focuses persistent item widgets even with NoEditTriggers,
		 * interrupting IME composition when search changes selection. */
		return false;
	}
};
}

HistoryView::HistoryView(HistoryService &history, QWidget *window)
	: MainFrame(window), m_history(history), m_window(window)
{
	setFocusPolicy(Qt::TabFocus);
	setupUi();
	m_interaction = new CardInteractionController(window, m_list, m_swipe, m_reflow, m_scroll);
	connect(m_interaction, &CardInteractionController::deleteRequested, this, [this](PasteItem *card) {
		m_list->setCurrentItem(card->widgetItem());
		removeCurrent();
	});
	connect(this, &MainFrame::moveFocusPrevNext, this, &HistoryView::moveSelection);
	connect(this, &MainFrame::hideWindow, this, &HistoryView::hideRequested);
	connect(this, &MainFrame::selectItem, this, [this](void) {
		if (PasteItem *card = currentCard()) { m_searchSelection = nullptr; card->copyData(); }
	});
	connect(this, &MainFrame::selectPlainTextItem, this, [this](void) {
		if (PasteItem *card = currentCard()) card->copyData(true);
	});
	connect(m_menuButton, &QPushButton::clicked, this, &HistoryView::menuRequested);
	connect(&m_history, &HistoryService::entryAdded, this, &HistoryView::addEntry);
	connect(&m_history, &HistoryService::entryRemoved, this, &HistoryView::removeEntry);
	connect(&m_history, &HistoryService::entryChanged, this, [this](EntryId id) {
		if (PasteItem *card = m_cards.value(id)) card->setIcon(QPixmap::fromImage(card->entry()->icon));
	});
	connect(&m_history, &HistoryService::loaded, this, [this](void) {
		m_list->setCurrentRow(0);
		resetItemTabOrder();
		updateSummary();
		emit countChanged();
	});
	connect(&m_history, &HistoryService::undoChanged, this, [this](bool available) {
		updateUndoState();
		if (!available) m_dismissals.clear();
	});
	setupShortcuts();
	qApp->installEventFilter(this);
}

void HistoryView::setupShortcuts(void)
{
	QShortcut *shortcut_search = new QShortcut(this);
	shortcut_search->setKey(QKeySequence("Ctrl+f"));
	QObject::connect(shortcut_search, &QShortcut::activated, [this](void) {
		LineEdit *lineedit = this->m_search->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly);
		lineedit->setFocus();
	});
	for (int number = 1; number <= 9; ++number) {
		QShortcut *quickPaste = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(number)), this);
		QObject::connect(quickPaste, &QShortcut::activated, this, [this, number](void) {
			this->pasteNumberedItem(number, false);
		});
		QShortcut *plainPaste = new QShortcut(QKeySequence(QString("Ctrl+Shift+%1").arg(number)), this);
		QObject::connect(plainPaste, &QShortcut::activated, this, [this, number](void) {
			this->pasteNumberedItem(number, true);
		});
	}
	QShortcut *plainSelected = new QShortcut(QKeySequence("Shift+Return"), this);
	QObject::connect(plainSelected, &QShortcut::activated, this, [this](void) {
		PasteItem *item = this->currentCard();
		if (item)
			item->copyData(true);
	});
}

void HistoryView::moveSelection(bool prev, bool wrap)
{
	this->m_scroll->cancel();
	if (this->m_swipe->sourceCard()) this->m_swipe->cancel();
	this->m_reflow->cancel();
	const int count = this->m_list->count();
	if (count == 0)
		return;

	int row = this->m_list->currentRow();
	if (row < 0)
		row = prev ? count : -1;
	const bool fromSearch = this->m_search->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly)->hasFocus();

	/* Bounded by the item count: if every item is hidden (search filtered
	 * everything out) this gives up instead of looping forever. */
	PasteItem *widget = nullptr;
	for (int i = 0; i < count; i++) {
		if (prev)
			--row;
		/* Tab advances the visible selection even while search has focus.
		 * Arrows from search still enter the currently highlighted result. */
		else if (wrap || i > 0 || !fromSearch || row < 0)
			++row;
		if (row < 0 || row >= count) {
			/* Arrow navigation stops at the edge; Tab may wrap around. */
			if (!wrap)
				return;
			row = prev ? count - 1 : 0;
		}

		QListWidgetItem *item = this->m_list->item(row);
		widget = reinterpret_cast<PasteItem *>(this->m_list->itemWidget(item));
		if (widget && !item->isHidden())
			break;
	}

	if (widget && !this->m_list->item(row)->isHidden()) {
		this->m_list->setCurrentRow(row);
		this->m_list->scrollToItem(this->m_list->item(row));
		widget->setFocus();
	}
}

PasteItem *HistoryView::currentCard(void) const
{
	QListWidgetItem *item = this->m_list->currentItem();
	if (!item || item->isHidden())
		return nullptr;

	return reinterpret_cast<PasteItem *>(this->m_list->itemWidget(item));
}

void HistoryView::updateSummary(void)
{
	int number = 0;
	for (int i = 0; i < this->m_list->count(); ++i) {
		QListWidgetItem *item = this->m_list->item(i);
		PasteItem *widget = reinterpret_cast<PasteItem *>(this->m_list->itemWidget(item));
		if (!widget)
			continue;
		widget->setQuickPasteNumber(item->isHidden() ? 0 : ++number);
	}
	if (this->m_count) {
		const int count = this->m_list->count();
		this->m_count->setText(number == count ? QObject::tr("%1 items").arg(count) :
					     QObject::tr("%1 of %2 items").arg(number).arg(count));
	}
	if (this->m_empty) {
		this->m_empty->setText(this->m_list->count() == 0 ?
			QObject::tr("Copy something to get started") : QObject::tr("No matching items"));
		this->m_empty->setGeometry(this->m_list->viewport()->rect());
		this->m_empty->setVisible(number == 0);
	}
}

void HistoryView::pasteNumberedItem(int number, bool plainText)
{
	int visible = 0;
	for (int i = 0; i < this->m_list->count(); ++i) {
		QListWidgetItem *item = this->m_list->item(i);
		if (!item->isHidden() && ++visible == number) {
			this->m_list->setCurrentItem(item);
			PasteItem *widget = reinterpret_cast<PasteItem *>(this->m_list->itemWidget(item));
			if (widget)
				widget->copyData(plainText);
			return;
		}
	}
}
void HistoryView::resetItemTabOrder(void)
{
	int count = this->m_list->count();

	for (int i = 0; i < count-1; i++) {
		QWidget *first = this->m_list->itemWidget(this->m_list->item(i));
		QWidget *second = this->m_list->itemWidget(this->m_list->item(i+1));
		QWidget::setTabOrder(first, second);
	}
}
void HistoryView::setupUi(void)
{
	this->m_search = new SearchBar(this,
					  qBound(260, m_window->width()/3, 360), 38);
	QObject::connect(this->m_search, &SearchBar::hideWindow, [this](void) {
		emit hideRequested();
	});
	QObject::connect(this->m_search, &SearchBar::textChanged, [this](const QString &text) {
		this->cancelInteractions();
		int temp_current_item_row = -1;
		int show_row_count = 0;

		/* Store current row num when first time searching */
		if (this->m_searchSelection == nullptr) {
			this->m_searchSelection = this->m_list->currentItem();
		}

		for (int i = 0; i < this->m_list->count(); i++) {
			QListWidgetItem *item = this->m_list->item(i);
			PasteItem *widget = reinterpret_cast<PasteItem *>(this->m_list->itemWidget(item));
			if (!widget->text().toLower().contains(text.toLower())) {
				item->setHidden(true);
			} else {
				item->setHidden(false);
				show_row_count++;

				if (temp_current_item_row == -1) {
					temp_current_item_row = i;
				}
			}
		}

		/* That is the first showing item */
		if (temp_current_item_row != -1) {
			this->m_list->setCurrentRow(temp_current_item_row);
			this->m_list->scrollToItem(this->m_list->item(temp_current_item_row));
		}

		if (show_row_count == this->m_list->count()) {
			/* restore current row in search before. The stored item may
			 * already have been removed by the dedup logic meanwhile. */
			if (this->m_searchSelection) {
				this->m_list->setCurrentItem(this->m_searchSelection);
				this->m_list->scrollToItem(this->m_searchSelection);
			}
			this->m_searchSelection = nullptr;
		}
		this->updateSummary();
	});
	QObject::connect(this->m_search, &SearchBar::selectItem, [this](void) {
		PasteItem *widget = this->currentCard();
		if (!widget)
			return;
		this->m_searchSelection = nullptr;
		widget->copyData();
	});
	QObject::connect(this->m_search, &SearchBar::selectPlainTextItem, this, [this](void) {
		PasteItem *widget = this->currentCard();
		if (widget)
			widget->copyData(true);
	});
	QObject::connect(this->m_search, &SearchBar::moveFocusPrevNext,
			 this, &HistoryView::moveSelection);

	this->m_menuButton = new RoundedButton(this);
	this->m_menuButton->setObjectName("PanelMenu");
	this->m_menuButton->setText(QStringLiteral("⋯"));
	this->m_menuButton->setToolTip(QObject::tr("Menu"));
	this->m_menuButton->setAccessibleName(QObject::tr("Menu"));
	this->m_menuButton->setFixedSize(36, 36);
	this->m_menuButton->setFlat(true);

	this->m_list = new HistoryList(this);
	this->m_list->setSelectionMode(QAbstractItemView::SingleSelection);
	this->m_list->setHorizontalScrollMode(QListWidget::ScrollPerPixel);
	this->m_list->setFlow(QListView::LeftToRight);
	this->m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	this->m_list->setViewMode(QListView::ListMode);
	this->m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
	this->m_list->setFrameShape(QListWidget::NoFrame);
	this->m_list->setSpacing(8);
	this->m_list->setWrapping(false);
	this->m_list->setFocusPolicy(Qt::NoFocus);
	this->m_scroll = new ElasticScrollController(this->m_list);
	this->m_list->viewport()->installEventFilter(this);
	QObject::connect(this->m_list, &QListWidget::currentItemChanged, this,
		[this](QListWidgetItem *current, QListWidgetItem *previous) {
		if (previous) {
			auto *widget = qobject_cast<PasteItem *>(this->m_list->itemWidget(previous));
			if (widget)
				widget->setSelected(false);
		}
		if (current) {
			auto *widget = qobject_cast<PasteItem *>(this->m_list->itemWidget(current));
			if (widget)
				widget->setSelected(true);
		}
		this->m_list->update();
	});

	this->m_empty = new QLabel(this->m_list->viewport());
	this->m_empty->setObjectName("EmptyState");
	this->m_empty->setAlignment(Qt::AlignCenter);
	this->m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);

	QLabel *brandIcon = new QLabel(this);
	brandIcon->setPixmap(QIcon(":/resources/pastes.svg").pixmap(QSize(30, 30), this->devicePixelRatioF()));
	brandIcon->setFixedSize(30, 30);
	QLabel *brandTitle = new QLabel(QObject::tr("Pastes"), this);
	brandTitle->setObjectName("BrandTitle");
	QLabel *historyTab = new RoundedLabel(QObject::tr("Clipboard"), RoundedRole::HistoryBadge, this);
	historyTab->setObjectName("HistoryTab");
	QHBoxLayout *hlayout = new QHBoxLayout();
	hlayout->setSpacing(10);
	hlayout->addWidget(brandIcon);
	hlayout->addWidget(brandTitle);
	hlayout->addSpacing(14);
	hlayout->addWidget(historyTab);
	this->m_recordingStatus = new QLabel(QObject::tr("Recording paused"), this);
	this->m_recordingStatus->setObjectName("RecordingStatus");
	this->m_recordingStatus->setVisible(!this->m_recordingEnabled);
	hlayout->addWidget(this->m_recordingStatus);
	hlayout->addStretch();
	hlayout->addWidget(this->m_search);
	hlayout->addWidget(this->m_menuButton);

	/* Keep animation outside the panel's shadow effect: moving a snapshot
	 * must not invalidate and blur the entire history panel every frame. */
	this->m_swipe = new CardSwipeOverlay(m_window);
	this->m_reflow = new CardReflowOverlay(m_window);
	this->m_count = new QLabel(this);
	this->m_count->setObjectName("HistoryCount");
	this->m_hint = new QLabel(this);
	this->m_hint->setObjectName("KeyboardHint");
	this->m_hint->setVisible(QSettings().value("showKeyboardHints", true).toBool());
	this->m_undoHint = new QLabel(QObject::tr("Removed from history"), this);
	this->m_undoHint->setObjectName("UndoHint");
	this->m_undoButton = new RoundedButton(this);
	this->m_undoButton->setObjectName("UndoButton");
	this->m_undoButton->setText(QObject::tr("Undo"));
	this->m_undoButton->setToolTip(QObject::tr("Undo deletion (Ctrl+Z)"));
	this->m_undoButton->setFocusPolicy(Qt::NoFocus);
	/* Align the painted button with labels, without native layout insets. */
	this->m_undoButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	this->m_undoButton->setFixedHeight(24);
	QObject::connect(this->m_undoButton, &QPushButton::clicked, this, &HistoryView::undo);
	this->m_undoShortcut = new QShortcut(QKeySequence("Ctrl+Z"), this);
	QObject::connect(this->m_undoShortcut, &QShortcut::activated, this, [this](void) {
		LineEdit *search = this->m_search->findChild<LineEdit *>();
		if (search->hasFocus() && search->isUndoAvailable()) search->undo();
		else this->undo();
	});
	this->updateUndoState();
	QHBoxLayout *footer = new QHBoxLayout();
	footer->setContentsMargins(8, 0, 8, 0);
	footer->setSpacing(12);
	footer->addWidget(this->m_count);
	footer->addWidget(this->m_undoHint);
	footer->addWidget(this->m_undoButton);
	footer->addStretch();
	footer->addWidget(this->m_hint);
	QWidget *footerWidget = new QWidget(this);
	footerWidget->setFixedHeight(24);
	footerWidget->setLayout(footer);

	QVBoxLayout *vlayout = new QVBoxLayout();
	vlayout->setContentsMargins(Platform::panelAppearance().margins);
	vlayout->setSpacing(Platform::panelAppearance().spacing);
	vlayout->addLayout(hlayout);
	vlayout->addWidget(this->m_list, 1);
	vlayout->addWidget(footerWidget);

	this->setLayout(vlayout);
	/* need this for resize this->m_list size */
	this->show();

}
void HistoryView::cancelInteractions(bool swipe, bool reflow, bool scroll)
{
	if (!m_interaction) return;
	m_interaction->reset(swipe, reflow, scroll);
	m_interaction->clearClick();
}

bool HistoryView::eventFilter(QObject *object, QEvent *event)
{
	if (m_interaction && m_window->isVisible() && m_interaction->handleEvent(object, event)) return true;
	if (object == m_list->viewport() && event->type() == QEvent::Resize && m_empty)
		m_empty->setGeometry(QRect(QPoint(), static_cast<QResizeEvent *>(event)->size()));
	if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
	    m_window->isVisible() && QApplication::activeWindow() == m_window) {
		LineEdit *search = m_search->findChild<LineEdit *>("", Qt::FindDirectChildrenOnly);
		auto *receiver = qobject_cast<QWidget *>(object);
		if (receiver && receiver->window() == m_window && object != search) {
			QKeyEvent *key = static_cast<QKeyEvent *>(event);
			if (key->key() == Qt::Key_Space) return MainFrame::eventFilter(object, event);
			const bool typing = !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
				!key->text().isEmpty() && key->text().at(0).isPrint();
			if (typing || (key->key() == Qt::Key_Backspace && !search->text().isEmpty())) {
				/* Cocoa queries the input-method target after ShortcutOverride.
				 * Focus search now so the first pinyin key can start composition
				 * instead of becoming literal text via the KeyPress fallback. */
				search->setFocus();
				if (event->type() == QEvent::KeyPress)
					QCoreApplication::sendEvent(search, event);
				event->accept();
				return true;
			}
		}
	}
	return MainFrame::eventFilter(object, event);
}

void HistoryView::resizeEvent(QResizeEvent *event)
{
	MainFrame::resizeEvent(event);
	if (!m_list) return;
	cancelInteractions();
	for (int row = 0; row < m_list->count(); ++row) m_list->item(row)->setSizeHint(cardSize());
	if (m_empty) m_empty->setGeometry(m_list->viewport()->rect());
}

QSize HistoryView::cardSize(void) const { return Platform::cardSize(m_window->size()); }
QWidget *HistoryView::menuAnchor(void) const { return m_menuButton; }

void HistoryView::focusCurrent(void)
{
	if (QListWidgetItem *item = m_list->currentItem()) {
		m_list->itemWidget(item)->setFocus();
		m_list->scrollToItem(item);
	}
}

void HistoryView::focusEntry(const QByteArray &md5)
{
	for (int row = 0; row < m_list->count(); ++row) {
		auto *card = qobject_cast<PasteItem *>(m_list->itemWidget(m_list->item(row)));
		if (card && card->entry()->md5 == md5) {
			m_list->setCurrentRow(row);
			if (currentCard()) currentCard()->setFocus();
			return;
		}
	}
}

void HistoryView::previewCurrent(void)
{
	cancelInteractions();
	if (PasteItem *card = currentCard()) emit previewRequested(card->entry());
}

void HistoryView::setRecordingEnabled(bool enabled)
{
	m_recordingEnabled = enabled;
	m_recordingStatus->setVisible(!enabled);
}

void HistoryView::setKeyboardHintsVisible(bool visible) { m_hint->setVisible(visible); }
void HistoryView::setPrimaryShortcut(const QString &shortcut)
{
	m_shortcutText = shortcut;
	updateShortcutHint();
}

void HistoryView::updateShortcutHint(void)
{
	m_hint->setText(QObject::tr("%1 Open   ·   ← → Browse   ·   Drag ↑ Delete   ·   Enter Paste   ·   Space Preview").arg(m_shortcutText));
}

void HistoryView::addEntry(HistoryEntry entry, int row, HistoryChange change)
{
	if (change == HistoryChange::Captured || change == HistoryChange::Synced) cancelInteractions();
	if (change == HistoryChange::Restored) m_reflow->prepare(m_list, nullptr);
	auto *item = new QListWidgetItem;
	auto *card = new PasteItem(nullptr, item);
	item->setSizeHint(cardSize());
	m_list->insertItem(row, item);
	m_list->setItemWidget(item, card);
	m_cards.insert(entry->id, card);
	connect(card, &PasteItem::copyRequested, this, &HistoryView::copyRequested);
	connect(card, &PasteItem::hideWindow, this, &HistoryView::hideRequested);
	connect(card, &PasteItem::moveFocusPrevNext, this, &HistoryView::moveSelection);
	connect(card, &PasteItem::previewRequested, this, [this, card](void) {
		m_list->setCurrentItem(card->widgetItem()); previewCurrent();
	});
	connect(card, &PasteItem::deleteRequested, this, [this, card](void) {
		m_list->setCurrentItem(card->widgetItem()); removeCurrent();
	});
	if (!card->setEntry(entry, change == HistoryChange::Loaded)) {
		m_history.discard(entry->id);
		return;
	}
	if (change == HistoryChange::Restored || change == HistoryChange::Synced) {
		const QString text = m_search->findChild<LineEdit *>()->text();
		item->setHidden(!card->text().contains(text, Qt::CaseInsensitive));
	}
	if (change == HistoryChange::Captured) m_list->setCurrentRow(0);
	card->setSelected(item->isSelected());
	/* The loaded signal finalizes the batch once. Rebuilding navigation and
	 * numbering for every persisted entry makes startup quadratic. */
	if (change != HistoryChange::Loaded) {
		resetItemTabOrder();
		updateSummary();
		emit countChanged();
	}
}

void HistoryView::removeEntry(EntryId id, HistoryChange change)
{
	PasteItem *card = m_cards.take(id);
	if (!card) return;
	QListWidgetItem *item = card->widgetItem();
	const int row = m_list->row(item);
	if (m_searchSelection == item) m_searchSelection = nullptr;
	const bool animate = change == HistoryChange::Deleted;
	if (!animate || m_interaction->isPressed(card) || m_swipe->sourceCard() == card) cancelInteractions();
	if (animate) m_reflow->prepare(m_list, card);
	m_list->removeItemWidget(item);
	delete item;
	card->hide();
	card->deleteLater();
	if (animate) {
		int next = -1;
		for (int i = row; i < m_list->count(); ++i)
			if (!m_list->item(i)->isHidden()) { next = i; break; }
		for (int i = qMin(row-1, m_list->count()-1); next < 0 && i >= 0; --i)
			if (!m_list->item(i)->isHidden()) { next = i; break; }
		m_list->setCurrentRow(next);
		if (next >= 0) m_list->itemWidget(m_list->item(next))->setFocus();
		else setFocus();
	}
	resetItemTabOrder();
	updateSummary();
	emit countChanged();
	if (animate) { layout()->activate(); m_reflow->animate(); }
}

void HistoryView::removeCurrent(void)
{
	m_scroll->cancel();
	PasteItem *card = currentCard();
	if (!card) return;
	const EntryId id = card->entry()->id;
	m_dismissals.insert(id, m_swipe->dismissalId(card));
	m_history.remove(id);
}

void HistoryView::updateUndoState(void)
{
	const bool available = m_history.canUndo();
	m_undoHint->setVisible(available);
	m_undoButton->setVisible(available);
	m_undoShortcut->setEnabled(available);
}

void HistoryView::undo(void)
{
	if (!m_history.canUndo()) return;
	cancelInteractions(false, false);
	if (m_swipe->sourceCard()) m_swipe->cancel();
	const quint64 dismissal = m_dismissals.take(m_history.nextUndoId());
	const auto result = m_history.undo();
	PasteItem *restored = result.entry ? m_cards.value(result.entry->id).data() : nullptr;
	if (restored && !restored->widgetItem()->isHidden()) {
		m_list->setCurrentItem(restored->widgetItem());
		m_list->scrollToItem(restored->widgetItem());
		restored->setFocus();
	}
	layout()->activate();
	if (restored && result.inserted) {
		m_list->doItemsLayout();
		m_reflow->animate();
		if (!restored->widgetItem()->isHidden() && m_window->isVisible()) m_swipe->restore(restored, dismissal);
		else m_swipe->cancel();
	} else {
		m_reflow->cancel();
		m_swipe->cancel();
	}
}
