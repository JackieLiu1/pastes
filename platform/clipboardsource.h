#ifndef CLIPBOARDSOURCE_H
#define CLIPBOARDSOURCE_H

#include <QObject>
#include <QImage>
#include <memory>

/* Notifications and source icons share one contract on every platform.
 * Native handles and worker state stay private to the selected backend. */
class ClipboardSource : public QObject
{
	Q_OBJECT
public:
	explicit ClipboardSource(QObject *parent = nullptr);
	~ClipboardSource();
	int settleInterval(void) const;
	void capture(quint64 request);
	/* True when a newer native copy invalidates the pending snapshot. */
	bool synchronize(void);
	QImage snapshotIcon(void);

signals:
	void clipboardChanged(void);
	void iconReady(quint64 request, QImage icon);

private:
	void checkClipboard(void);
	class Private;
	std::unique_ptr<Private> m_private;
};

#endif
