#ifndef CLIPBOARDSOURCE_H
#define CLIPBOARDSOURCE_H

#include "application/clipboardfeed.h"
#include <QImage>
#include <memory>

/* Notifications and source icons share one contract on every platform.
 * Native handles and worker state stay private to the selected backend. */
class ClipboardSource : public ClipboardFeed
{
	Q_OBJECT
public:
	explicit ClipboardSource(QObject *parent = nullptr);
	~ClipboardSource() override;
	int settleInterval(void) const override;
	void capture(quint64 request) override;
	/* True when a newer native copy invalidates the pending snapshot. */
	bool synchronize(void) override;
	bool allowsCapture(void) const override;
	QImage snapshotIcon(void) override;

private:
	void checkClipboard(void);
	class Private;
	std::unique_ptr<Private> m_private;
};

#endif
