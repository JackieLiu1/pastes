#ifndef PASTETARGET_H
#define PASTETARGET_H

#include <QObject>
#include <memory>

class QWidget;

/* Preserve the destination independently of clipboard items and hide
 * animations. The backend owns native focus restoration and injection. */
class PasteTarget : public QObject
{
	Q_OBJECT
public:
	explicit PasteTarget(QObject *parent = nullptr);
	~PasteTarget();
	void captureTarget(QWidget *panel);
	void paste(QWidget *panel, bool hasUrls);
	void cancel(void);
	static void requestPermission(void);

signals:
	void permissionRequired(void);

private:
	void tryPaste(void);
	void notifyPermissionRequired(void);
	class Private;
	std::unique_ptr<Private> m_private;
};

#endif
