#include "settingsdialog.h"
#include "roundedwidgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QButtonGroup>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QStyle>
#include <QVariantAnimation>
#include <QVBoxLayout>

class SettingsSwitch : public QAbstractButton
{
public:
	SettingsSwitch(const QString &label, bool checked, QWidget *parent) : QAbstractButton(parent)
	{
		setCheckable(true); setChecked(checked);
		setFixedSize(46, 28); setFocusPolicy(Qt::StrongFocus);
		setAccessibleName(label); setToolTip(label);
		m_position = checked ? 1 : 0;
		m_animation.setDuration(140);
		m_animation.setEasingCurve(QEasingCurve::OutCubic);
		QObject::connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
			m_position = value.toReal(); update();
		});
		QObject::connect(this, &QAbstractButton::toggled, this, [this](bool value) {
			m_animation.stop(); m_animation.setStartValue(m_position);
			m_animation.setEndValue(value ? 1.0 : 0.0); m_animation.start();
		});
	}
	void restoreChecked(bool checked)
	{
		QSignalBlocker blocker(this);
		setChecked(checked); m_animation.stop(); m_position = checked ? 1 : 0; update();
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		const bool dark = qApp->property("pastesDark").toBool();
		const QColor off(dark ? "#454545" : "#CAD3C7");
		const QColor on(dark ? "#76C5AA" : "#237B68");
		QColor fill = QColor::fromRgbF(off.redF()+(on.redF()-off.redF())*m_position,
			off.greenF()+(on.greenF()-off.greenF())*m_position,
			off.blueF()+(on.blueF()-off.blueF())*m_position);
		QPainter painter(this); painter.setRenderHint(QPainter::Antialiasing);
		if (!isEnabled()) painter.setOpacity(0.4);
		painter.setPen(hasFocus() ? QPen(on, 1) : QPen(Qt::NoPen));
		painter.setBrush(fill); painter.drawRoundedRect(QRectF(2, 3, 42, 22), 11, 11);
		painter.setPen(Qt::NoPen); painter.setBrush(QColor("#FFFFFF"));
		painter.drawEllipse(QRectF(5+20*m_position, 6, 16, 16));
	}

private:
	QVariantAnimation m_animation;
	qreal m_position = 0;
};

static QVBoxLayout *section(QVBoxLayout *parent, const QString &title, QWidget *owner)
{
	auto *group = new QVBoxLayout;
	group->setSpacing(8);
	auto *heading = new QLabel(title, owner);
	heading->setObjectName("SettingsSectionTitle");
	group->addWidget(heading);
	auto *card = new RoundedWidget(RoundedRole::PreviewContent, owner);
	card->setObjectName("AppDialogCard");
	auto *rows = new QVBoxLayout(card);
	rows->setContentsMargins(16, 12, 16, 12); rows->setSpacing(10);
	group->addWidget(card); parent->addLayout(group);
	return rows;
}

static void settingRow(QVBoxLayout *parent, const QString &title, const QString &description, QWidget *control)
{
	auto *row = new QHBoxLayout;
	row->setSpacing(16);
	auto *texts = new QVBoxLayout;
	texts->setSpacing(4);
	auto *label = new QLabel(title);
	label->setObjectName("SettingsRowTitle");
	auto *detail = new QLabel(description);
	detail->setObjectName("AppDialogMuted");
	detail->setWordWrap(true);
	texts->addWidget(label); texts->addWidget(detail);
	row->addLayout(texts, 1); row->addWidget(control, 0, Qt::AlignVCenter);
	parent->addLayout(row);
}

static void divider(QVBoxLayout *rows)
{
	auto *line = new QFrame;
	line->setObjectName("SettingsDivider"); line->setFixedHeight(1);
	rows->addWidget(line);
}

SettingsDialog::SettingsDialog(const QString &shortcut, QWidget *parent) :
	AppDialog(QObject::tr("Settings"), 568, parent),
	m_status(new QLabel(this)), m_shortcut(nullptr), m_themes(new QButtonGroup(this))
{
	setSubtitle(QObject::tr("Appearance, startup and clipboard history."));
	auto *scroll = new QScrollArea(this);
	scroll->setObjectName("SettingsScroll");
	scroll->setFrameShape(QFrame::NoFrame); scroll->setWidgetResizable(true);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->verticalScrollBar()->setObjectName("PreviewScroll");
	QScreen *screen = parent ? parent->screen() : QGuiApplication::primaryScreen();
	auto *content = new QWidget;
	content->setObjectName("SettingsBody");
	auto *groups = new QVBoxLayout(content);
	groups->setContentsMargins(0, 0, 4, 0); groups->setSpacing(16);
	QSettings preferences;
	auto *appearance = section(groups, QObject::tr("Appearance"), content);
	auto *choices = new QWidget(content);
	auto *buttons = new QHBoxLayout(choices);
	buttons->setContentsMargins(0, 0, 0, 0); buttons->setSpacing(4);
	const QString theme = preferences.value("theme", "light").toString();
	for (int i = 0; i < 2; ++i) {
		auto *button = new RoundedButton(choices);
		button->setObjectName("SettingsChoice"); button->setCheckable(true);
		button->setText(i == 0 ? QObject::tr("Light") : QObject::tr("Dark"));
		button->setAutoDefault(false); button->setFixedSize(68, 36);
		m_themes->addButton(button, i);
		button->setChecked((i == 0) == (theme == "light")); buttons->addWidget(button);
	}
	updateThemeChoices();
	QObject::connect(m_themes, &QButtonGroup::idClicked, this, [this](int id) {
		const QString theme = id == 0 ? QStringLiteral("light") : QStringLiteral("dark");
		if (savePreference(QStringLiteral("theme"), theme)) emit themeChanged(theme);
		else m_themes->button(qApp->property("pastesDark").toBool() ? 1 : 0)->setChecked(true);
		updateThemeChoices();
	});
	settingRow(appearance, QObject::tr("Color theme"), QObject::tr("For the panel and its windows."), choices);
	divider(appearance);
	auto *hints = new SettingsSwitch(QObject::tr("Keyboard hints"), preferences.value("showKeyboardHints", true).toBool(), content);
	hints->setObjectName("KeyboardHintsSwitch");
	settingRow(appearance, QObject::tr("Keyboard hints"), QObject::tr("Show the controls below your cards."), hints);
	QObject::connect(hints, &QAbstractButton::toggled, this, [this,hints](bool checked) {
		if (savePreference(QStringLiteral("showKeyboardHints"), checked)) emit hintsChanged(checked);
		else hints->restoreChecked(!checked);
	});
	auto *general = section(groups, QObject::tr("General"), content);
	auto *startup = new SettingsSwitch(QObject::tr("Launch at sign-in"), m_startup.enabled(), content);
	startup->setObjectName("StartupSwitch"); startup->setEnabled(m_startup.supported());
	settingRow(general, QObject::tr("Launch at sign-in"), m_startup.supported() ?
		QObject::tr("Ready in the tray when you need it.") : QObject::tr("Startup is not available on this platform yet."), startup);
	QObject::connect(startup, &QAbstractButton::toggled, this, [this,startup](bool checked) {
		QString error;
		if (!m_startup.setEnabled(checked, &error)) {
			startup->restoreChecked(m_startup.enabled());
			showError(error);
		} else showError(QString());
	});
	divider(general);
	auto *pause = new SettingsSwitch(QObject::tr("Pause clipboard recording"), preferences.value("pauseRecording", false).toBool(), content);
	pause->setObjectName("PauseRecordingSwitch");
	settingRow(general, QObject::tr("Pause clipboard recording"), QObject::tr("Keep existing history; skip new copies."), pause);
	QObject::connect(pause, &QAbstractButton::toggled, this, [this,pause](bool checked) {
		if (savePreference(QStringLiteral("pauseRecording"), checked)) emit recordingChanged(!checked);
		else pause->restoreChecked(!checked);
	});
	auto *keys = section(groups, QObject::tr("Shortcuts"), content);
	auto shortcutRow = [&](const QString &label, const QString &key) {
		auto *row = new QHBoxLayout;
		auto *caption = new QLabel(label, content); caption->setObjectName("AppDialogValue");
		auto *value = new RoundedLabel(key, RoundedRole::Number, content); value->setObjectName("SettingsKey");
		row->addWidget(caption); row->addStretch(); row->addWidget(value);
		keys->addLayout(row); return value;
	};
	m_shortcut = shortcutRow(QObject::tr("Open history"), shortcut);
	shortcutRow(QObject::tr("Paste selected item"), QObject::tr("Enter / Double-click"));
	shortcutRow(QObject::tr("Preview selected item"), QStringLiteral("Space"));
	shortcutRow(QObject::tr("Search history"), QStringLiteral("Ctrl+F"));
	groups->addStretch(); scroll->setWidget(content);
	/* Fit the styled content when the screen allows it, instead of
	 * introducing a tiny scroll range that clips the first heading. */
	content->ensurePolished();
	const int availableHeight = qMax(180, screen ? screen->availableGeometry().height()-210 : 490);
	scroll->setFixedHeight(qBound(180, content->minimumSizeHint().height(), availableHeight));
	bodyLayout()->addWidget(scroll);
	m_status->setObjectName("SettingsStatus"); m_status->setWordWrap(true);
	m_status->setText(QObject::tr("Changes are saved automatically."));
	auto *done = new RoundedButton(this);
	done->setObjectName("PreviewAction"); done->setProperty("primary", true);
	done->setText(QObject::tr("Done")); done->setDefault(true);
	QObject::connect(done, &QPushButton::clicked, this, &QDialog::accept);
	auto *footer = new QHBoxLayout;
	footer->setSpacing(16); footer->addWidget(m_status, 1); footer->addWidget(done);
	bodyLayout()->addLayout(footer);
	adjustSize();
}

void SettingsDialog::setPrimaryShortcut(const QString &shortcut)
{
	m_shortcut->setText(shortcut);
}

bool SettingsDialog::savePreference(const QString &key, const QVariant &value)
{
	QSettings preferences;
	preferences.setValue(key, value); preferences.sync();
	if (preferences.status() != QSettings::NoError) {
		showError(QObject::tr("Could not save settings. Check your account permissions and try again."));
		return false;
	}
	showError(QString());
	return true;
}

void SettingsDialog::showError(const QString &error)
{
	m_status->setText(error.isEmpty() ? QObject::tr("Changes are saved automatically.") : error);
	m_status->setProperty("error", !error.isEmpty());
	m_status->style()->unpolish(m_status); m_status->style()->polish(m_status);
}

void SettingsDialog::updateThemeChoices(void)
{
	for (QAbstractButton *button : m_themes->buttons()) {
		button->setProperty("primary", button->isChecked());
		button->style()->unpolish(button); button->style()->polish(button); button->update();
	}
}
