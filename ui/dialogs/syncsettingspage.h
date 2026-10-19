#ifndef PASTES_SYNCSETTINGSPAGE_H
#define PASTES_SYNCSETTINGSPAGE_H
#include <QWidget>
class SyncService;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class SyncSettingsPage final : public QWidget
{
	Q_OBJECT
public:
	explicit SyncSettingsPage(SyncService &sync, QWidget *parent = nullptr);
private:
	bool save(void);
	void refresh(void);
	SyncService &m_sync;
	QLineEdit *m_url, *m_username, *m_password;
	QCheckBox *m_enabled;
	QLabel *m_status;
	QPushButton *m_save, *m_test, *m_now;
};
#endif
