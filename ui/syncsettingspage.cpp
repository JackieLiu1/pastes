#include "ui/syncsettingspage.h"
#include "ui/roundedwidgets.h"
#include "application/syncservice.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QLocale>

SyncSettingsPage::SyncSettingsPage(SyncService &sync, QWidget *parent) : QWidget(parent), m_sync(sync)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(12);
	auto *intro = new QLabel(tr("Your history, across devices"), this);
	intro->setObjectName("SettingsRowTitle"); layout->addWidget(intro);
	auto *description = new QLabel(tr("Sync text, links and images through your own WebDAV server. Files and folders stay on this device."), this);
	description->setObjectName("AppDialogMuted"); description->setWordWrap(true); layout->addWidget(description);
	auto *card = new RoundedWidget(RoundedRole::PreviewContent, this);
	card->setObjectName("AppDialogCard");
	auto *form = new QFormLayout(card);
	form->setContentsMargins(16, 12, 16, 12); form->setSpacing(10);
	form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
	form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	const auto settings = sync.settings();
	m_url = new QLineEdit(settings.url.toDisplayString(), card);
	m_url->setPlaceholderText("https://dav.example.com/folder/"); m_url->setObjectName("SyncAddress");
	m_username = new QLineEdit(settings.username, card); m_username->setObjectName("SyncUsername");
	m_password = new QLineEdit(card); m_password->setObjectName("SyncPassword");
	m_password->setEchoMode(QLineEdit::Password);
	m_password->setPlaceholderText(QString(8, QChar(0x2022)));
	m_password->setToolTip(tr("Leave blank to keep the saved password"));
	for (auto *edit : {m_url, m_username, m_password}) { edit->setMinimumHeight(30); edit->setProperty("syncField", true); }
	form->addRow(tr("Server folder"), m_url);
	form->addRow(tr("Account"), m_username);
	form->addRow(tr("Password / app password"), m_password);
	m_enabled = new QCheckBox(tr("Enable automatic sync"), card);
	m_enabled->setObjectName("SyncEnabled"); m_enabled->setChecked(settings.enabled);
	form->addRow(QString(), m_enabled); layout->addWidget(card);
	auto *actions = new QHBoxLayout;
	auto button = [this,actions](const QString &text, const QString &name) {
		auto *button = new RoundedButton(this);
		button->setText(text); button->setObjectName("SyncAction"); button->setAccessibleName(name);
		button->setAutoDefault(false); actions->addWidget(button); return button;
	};
	m_save = button(tr("Save connection"), tr("Save connection")); m_save->setProperty("primary", true);
	m_test = button(tr("Test connection"), tr("Test connection"));
	m_now = button(tr("Sync now"), tr("Sync now"));
	actions->addStretch(); layout->addLayout(actions);
	m_status = new QLabel(this); m_status->setObjectName("SyncStatus"); m_status->setWordWrap(true);
	m_status->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(m_status);
	auto *note = new QLabel(tr("Each item keeps its copy time. History is retained for 30 days. Items over 20 MB or images over 20 megapixels are skipped. The server can read the synced content; use a private account."), this);
	note->setObjectName("AppDialogMuted"); note->setWordWrap(true); layout->addWidget(note);
	layout->addStretch();
	connect(m_save, &QPushButton::clicked, this, [this] { save(); });
	connect(m_test, &QPushButton::clicked, this, [this] { if (save()) m_sync.testConnection(); });
	connect(m_now, &QPushButton::clicked, this, [this] { if (save()) m_sync.synchronize(); });
	connect(&sync, &SyncService::statusChanged, this, &SyncSettingsPage::refresh);
	refresh();
}
bool SyncSettingsPage::save(void)
{
	SyncSettings settings;
	settings.url = QUrl(m_url->text().trimmed()); settings.username = m_username->text().trimmed();
	settings.enabled = m_enabled->isChecked();
	QString error;
	if (!m_sync.save(settings, m_password->text(), &error)) { m_status->setText(error); return false; }
	m_password->clear(); refresh(); return true;
}
void SyncSettingsPage::refresh(void)
{
	QString status = m_sync.status();
	if (m_sync.lastSuccess().isValid()) status += "\n"+tr("Last sync: %1").arg(QLocale().toString(m_sync.lastSuccess().toLocalTime(), QLocale::ShortFormat));
	m_status->setText(status);
	for (auto *button : {m_save,m_test,m_now}) button->setEnabled(!m_sync.busy());
}
