#ifndef CLIPBOARDSOURCE_WIN_H
#define CLIPBOARDSOURCE_WIN_H

#include <QObject>
#include <QImage>
#include <QCache>
#include <QThread>

/* Capture the owner while the clipboard changes, then resolve executable
 * icons on a worker. Only QImage values cross the thread boundary. */
class ClipboardSource : public QObject
{
	Q_OBJECT
public:
	explicit ClipboardSource(QObject *parent = nullptr);
	~ClipboardSource();
	void capture(quint64 request);

signals:
	void iconReady(quint64 request, QImage icon);

private:
	QObject *m_worker;
	QThread m_thread;
	QCache<QString, QImage> m_icons{64};
};

#endif
