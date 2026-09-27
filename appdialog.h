#ifndef APPDIALOG_H
#define APPDIALOG_H

#include <QDialog>

class QLabel;
class QVBoxLayout;
class RoundedWidget;

/* Shared window chrome for small application dialogs. */
class AppDialog : public QDialog
{
public:
	AppDialog(const QString &title, int width, QWidget *parent = nullptr);

protected:
	QVBoxLayout *bodyLayout(void) const;
	void setSubtitle(const QString &text);
	bool eventFilter(QObject *object, QEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	RoundedWidget *m_surface;
	QWidget *m_header;
	QLabel *m_subtitle;
	QVBoxLayout *m_body;
};

class AboutDialog : public AppDialog
{
public:
	explicit AboutDialog(QWidget *parent = nullptr);
};

#endif
