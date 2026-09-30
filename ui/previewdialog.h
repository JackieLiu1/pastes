#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>

struct ItemData;
class QLabel;
class RoundedWidget;

class PreviewDialog : public QDialog
{
	Q_OBJECT
public:
	explicit PreviewDialog(const ItemData &data, QWidget *parent = nullptr);
	bool plainText(void) const { return m_plain_text; }

protected:
	bool eventFilter(QObject *object, QEvent *event) override;
	void showEvent(QShowEvent *event) override;
	void changeEvent(QEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;

signals:
	void copyRequested(void);

private:
	RoundedWidget *m_surface;
	QWidget *m_header;
	QLabel *m_detail;
	bool m_plain_text = false;
};

#endif
