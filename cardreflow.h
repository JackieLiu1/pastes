#ifndef CARDREFLOW_H
#define CARDREFLOW_H

#include <QPixmap>
#include <QPointer>
#include <QWidget>
#include <vector>

class QListWidget;
class PasteItem;
class QPropertyAnimation;

/* Positions are recorded before the model changes. Cached GUI snapshots
 * slide over the list while the real rows already occupy their final slots. */
class CardReflowOverlay : public QWidget
{
	Q_OBJECT
	Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
	explicit CardReflowOverlay(QWidget *parent);
	void prepare(QListWidget *list, QWidget *removed);
	void animate(void);
	void cancel(void);
	qreal progress(void) const { return m_progress; }
	void setProgress(qreal progress);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	struct MovingCard {
		QPointer<PasteItem> card;
		QPointF start;
		QPointF end;
		QSize size;
		QPixmap snapshot;
	};
	std::vector<MovingCard> m_cards;
	QPointer<QListWidget> m_list;
	QPropertyAnimation *m_animation;
	qreal m_progress = 0;
};

#endif
