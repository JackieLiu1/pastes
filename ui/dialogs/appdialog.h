#ifndef APPDIALOG_H
#define APPDIALOG_H

#include <QDialog>

class QLabel;
class QVBoxLayout;
class RoundedWidget;
class QLineEdit;

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
	void changeEvent(QEvent *event) override;

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

class FavoriteNameDialog : public AppDialog
{
public:
	explicit FavoriteNameDialog(const QString &name, QWidget *parent = nullptr);
	QString name(void) const;
private:
	QLineEdit *m_name;
};

class PastePermissionDialog : public AppDialog
{
public:
	explicit PastePermissionDialog(QWidget *parent = nullptr);
};

#endif
