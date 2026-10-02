#ifndef PASTES_HISTORYVIEW_H
#define PASTES_HISTORYVIEW_H

#include "ui/mainframe.h"
#include "application/historyservice.h"
#include <QHash>
#include <QPointer>

class SearchBar;
class PasteItem;
class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class QShortcut;
class CardSwipeOverlay;
class CardReflowOverlay;
class ElasticScrollController;
class CardInteractionController;

/* History presentation: card binding, filtering, selection and animations.
 * Commands go to HistoryService; the window handles copy/paste and dialogs. */
class HistoryView final : public MainFrame
{
	Q_OBJECT
public:
	HistoryView(HistoryService &history, QWidget *window);
	void cancelInteractions(bool swipe = true, bool reflow = true, bool scroll = true);
	void focusCurrent(void);
	void focusEntry(const QByteArray &md5);
	void setPrimaryShortcut(const QString &shortcut);
	void setRecordingEnabled(bool enabled);
	void setKeyboardHintsVisible(bool visible);
	QWidget *menuAnchor(void) const;

signals:
	void hideRequested(void);
	void menuRequested(void);
	void copyRequested(HistoryEntry entry, bool plainText, bool paste);
	void previewRequested(HistoryEntry entry);
	void countChanged(void);

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private:
	void setupUi(void);
	void applyFilter(bool resetSelection = false);
	void updateEntry(EntryId id);
	bool matchesFilter(PasteItem *card) const;
	void setupShortcuts(void);
	void addEntry(HistoryEntry entry, int row, HistoryChange change);
	void removeEntry(EntryId id, HistoryChange change);
	void removeCurrent(void);
	void undo(void);
	void updateUndoState(void);
	void updateSummary(void);
	void updateShortcutHint(void);
	void resetItemTabOrder(void);
	void moveSelection(bool previous, bool wrap);
	void pasteNumberedItem(int number, bool plainText);
	void previewCurrent(void);
	PasteItem *currentCard(void) const;
	QSize cardSize(void) const;

	HistoryService &m_history;
	QWidget *m_window;
	SearchBar *m_search = nullptr;
	QListWidget *m_list = nullptr;
	QListWidgetItem *m_searchSelection = nullptr;
	QHash<EntryId, QPointer<PasteItem>> m_cards;
	QHash<EntryId, quint64> m_dismissals;
	QLabel *m_count = nullptr;
	QLabel *m_hint = nullptr;
	QLabel *m_recordingStatus = nullptr;
	QLabel *m_empty = nullptr;
	QLabel *m_undoHint = nullptr;
	QPushButton *m_menuButton = nullptr;
	QPushButton *m_historyTab = nullptr;
	QPushButton *m_favoritesTab = nullptr;
	QPushButton *m_undoButton = nullptr;
	QShortcut *m_undoShortcut = nullptr;
	CardSwipeOverlay *m_swipe = nullptr;
	CardReflowOverlay *m_reflow = nullptr;
	ElasticScrollController *m_scroll = nullptr;
	CardInteractionController *m_interaction = nullptr;
	QString m_shortcutText;
	bool m_recordingEnabled = true;
	bool m_favoritesOnly = false;
};

#endif
