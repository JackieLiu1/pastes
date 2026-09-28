#ifndef PASTE_MAC_H
#define PASTE_MAC_H

#include <QObject>
#include <memory>

class QWidget;

/* Keep the destination independent of the history item's lifetime. */
class MacPasteController : public QObject
{
	Q_OBJECT
public:
	explicit MacPasteController(QObject *parent = nullptr);
	~MacPasteController();
	void captureTarget(void);
	void paste(QWidget *panel);
	void cancel(void);

signals:
	void permissionRequired(void);

private:
	void tryPaste(void);
	void notifyPermissionRequired(void);
	class Private;
	std::unique_ptr<Private> m_private;
};

void requestMacPastePermission(void);

#endif // PASTE_MAC_H
