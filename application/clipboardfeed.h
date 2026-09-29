#ifndef PASTES_CLIPBOARDFEED_H
#define PASTES_CLIPBOARDFEED_H

#include <QObject>
#include <QImage>

/* OS adapter port. All public calls and notifications belong to the GUI
 * thread; platform workers only deliver QImage values and request IDs. */
class ClipboardFeed : public QObject
{
	Q_OBJECT
public:
	using QObject::QObject;
	virtual int settleInterval(void) const = 0;
	virtual void capture(quint64 request) = 0;
	virtual bool synchronize(void) = 0;
	/* Inspect native metadata before requesting any clipboard payload. */
	virtual bool allowsCapture(void) const = 0;
	virtual QImage snapshotIcon(void) = 0;

signals:
	void clipboardChanged(void);
	void iconReady(quint64 request, QImage icon);
};

#endif
