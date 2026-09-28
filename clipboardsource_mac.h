#ifndef CLIPBOARDSOURCE_MAC_H
#define CLIPBOARDSOURCE_MAC_H

#include <QObject>
#include <QImage>
#include <memory>

/* Observe native changes while Qt is inactive, keeping the source tied
 * to the copy rather than the later activation of the history panel. */
class ClipboardSource : public QObject
{
	Q_OBJECT
public:
	explicit ClipboardSource(QObject *parent = nullptr);
	~ClipboardSource();
	void capture(quint64 request);
	/* Return true when the native pasteboard changed since the last check. */
	bool synchronize(void);

signals:
	void clipboardChanged(void);
	void iconReady(quint64 request, QImage icon);

private:
	void checkClipboard(void);
	class Private;
	std::unique_ptr<Private> m_private;
};

#endif // CLIPBOARDSOURCE_MAC_H
