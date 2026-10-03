#include "ui/appdialog.h"
#include "core/historypolicy.h"
#include "core/itemdata.h"
#include "platform/pastetarget.h"
#include "ui/roundedwidgets.h"
#include "platform/windowintegration.h"

#include <QApplication>
#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

AppDialog::AppDialog(const QString &title, int width, QWidget *parent) :
	QDialog(parent, Platform::dialogAppearance().flags),
	m_surface(new RoundedWidget(RoundedRole::Preview, this)),
	m_header(new QWidget(m_surface)), m_subtitle(new QLabel(m_header)),
	m_body(new QVBoxLayout)
{
	setObjectName("AppDialog");
	setWindowTitle(title);
	setAttribute(Qt::WA_TranslucentBackground);
	Platform::initializeDialog(this);
	setModal(true);
	setFixedWidth(width);
	setFont(QFont(QStringLiteral("Segoe UI"), 10));
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(Platform::dialogAppearance().outerMargins);
	outer->addWidget(m_surface);
	m_surface->setObjectName("AppDialogSurface");
	if (!Platform::dialogAppearance().nativeControls) {
		auto *shadow = new QGraphicsDropShadowEffect(m_surface);
		shadow->setOffset(0, 4);
		shadow->setBlurRadius(24);
		shadow->setColor(qApp->property("pastesDark").toBool() ? QColor(0, 0, 0, 75) : QColor(12, 30, 23, 45));
		m_surface->setGraphicsEffect(shadow);
	}
	auto *layout = new QVBoxLayout(m_surface);
	layout->setContentsMargins(Platform::dialogAppearance().contentMargins);
	layout->setSpacing(22);
	auto *heading = new QLabel(title, m_header);
	heading->setObjectName("AppDialogTitle");
	m_subtitle->setObjectName("AppDialogMuted");
	m_subtitle->setWordWrap(true);
	m_subtitle->hide();
	auto *titles = new QVBoxLayout;
	titles->setSpacing(5);
	titles->addWidget(heading);
	titles->addWidget(m_subtitle);

	auto *header = new QHBoxLayout(m_header);
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(14);
	header->addLayout(titles, 1);
	if (!Platform::dialogAppearance().nativeControls) {
		auto *close = new DialogCloseButton(m_header);
		QObject::connect(close, &QPushButton::clicked, this, &QDialog::reject);
		header->addWidget(close, 0, Qt::AlignTop);
	}
	this->installEventFilter(this);
	m_surface->installEventFilter(this);
	m_header->installEventFilter(this);
	for (QLabel *label : m_header->findChildren<QLabel *>())
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	layout->addWidget(m_header);
	m_body->setSpacing(20);
	layout->addLayout(m_body);
}

QVBoxLayout *AppDialog::bodyLayout(void) const
{
	return m_body;
}

void AppDialog::setSubtitle(const QString &text)
{
	m_subtitle->setText(text);
	m_subtitle->setVisible(!text.isEmpty());
}

bool AppDialog::eventFilter(QObject *object, QEvent *event)
{
	if ((object == this || object == m_surface || object == m_header) &&
		event->type() == QEvent::MouseButtonPress) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		/* Include the padding above and beside the heading, plus half the
		 * gap before the body. Buttons keep handling their own presses. */
		const QPoint point = mapFromGlobal(mouse->globalPosition().toPoint());
		const int bottom = m_header->mapTo(this, QPoint(0, m_header->height())).y()
			+ m_surface->layout()->spacing()/2;
		if (mouse->button() == Qt::LeftButton && rect().contains(point) &&
			point.y() < bottom && windowHandle()) {
			windowHandle()->startSystemMove();
			return true;
		}
	}
	return QDialog::eventFilter(object, event);
}

void AppDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	Platform::prepareDialog(this);
	QScreen *screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
	if (!screen) return;
	const QRect area = screen->availableGeometry();
	move(area.center()-QPoint(width()/2, height()/2));
	Platform::updateDialogBackdrop(this, m_surface);
}

void AppDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::StyleChange)
		Platform::prepareDialog(this);
	if (event->type() == QEvent::StyleChange) {
		if (isVisible()) Platform::updateDialogBackdrop(this, m_surface);
		if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(m_surface->graphicsEffect()))
			shadow->setColor(qApp->property("pastesDark").toBool() ? QColor(0, 0, 0, 75) : QColor(12, 30, 23, 45));
	}
}

FavoriteNameDialog::FavoriteNameDialog(const QString &name, QWidget *parent) :
	AppDialog(QObject::tr("Name Favorite"), 440, parent), m_name(new QLineEdit(this))
{
	setSubtitle(QObject::tr("Give this favorite a name. Its copied content stays the same."));
	m_name->setObjectName("FavoriteNameEdit");
	m_name->setProperty("syncField", true);
	m_name->setMaxLength(FavoriteDetails::maxNameLength);
	m_name->setPlaceholderText(QObject::tr("Leave empty to show only the content"));
	m_name->setText(name);
	m_name->setMinimumHeight(36);
	bodyLayout()->addWidget(m_name);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch();
	auto *cancel = new RoundedButton(this);
	cancel->setObjectName("PreviewAction"); cancel->setText(QObject::tr("Cancel"));
	cancel->setAutoDefault(false);
	connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
	buttons->addWidget(cancel);
	auto *save = new RoundedButton(this);
	save->setObjectName("PreviewAction"); save->setProperty("primary", true);
	save->setText(QObject::tr("Save")); save->setDefault(true);
	connect(save, &QPushButton::clicked, this, &QDialog::accept);
	buttons->addWidget(save);
	bodyLayout()->addLayout(buttons);
	m_name->setFocus();
	m_name->selectAll();
	adjustSize();
}

QString FavoriteNameDialog::name(void) const { return m_name->text(); }

PastePermissionDialog::PastePermissionDialog(QWidget *parent) :
	AppDialog(QObject::tr("Allow direct paste"), 492, parent)
{
	auto *description = new QLabel(QObject::tr("To paste into the previous app, allow Pastes to control other apps in System Settings → Privacy & Security."), this);
	description->setObjectName("AppDialogValue");
	description->setWordWrap(true);
	bodyLayout()->addWidget(description);
	const QString recoveryHint = PasteTarget::permissionRecoveryHint();
	if (!recoveryHint.isEmpty()) {
		auto *recovery = new QLabel(recoveryHint, this);
		recovery->setObjectName("AppDialogValue");
		recovery->setTextFormat(Qt::PlainText);
		recovery->setWordWrap(true);
		recovery->setTextInteractionFlags(Qt::TextSelectableByMouse);
		bodyLayout()->addWidget(recovery);
	}
	auto *fallback = new QLabel(QObject::tr("The selected item is on the clipboard. You can press ⌘V to paste it."), this);
	fallback->setObjectName("AppDialogMuted");
	fallback->setWordWrap(true);
	bodyLayout()->addWidget(fallback);
	auto *buttons = new QHBoxLayout;
	buttons->addStretch();
	auto *later = new RoundedButton(this);
	later->setObjectName("PreviewAction"); later->setText(QObject::tr("Not now"));
	later->setAutoDefault(false);
	QObject::connect(later, &QPushButton::clicked, this, &QDialog::reject);
	buttons->addWidget(later);
	auto *allow = new RoundedButton(this);
	allow->setObjectName("PreviewAction"); allow->setProperty("primary", true);
	allow->setText(QObject::tr("Open System Settings"));
	QObject::connect(allow, &QPushButton::clicked, this, [this](void) {
		PasteTarget::requestPermission();
		accept();
	});
	buttons->addWidget(allow);
	bodyLayout()->addLayout(buttons);
}

AboutDialog::AboutDialog(QWidget *parent) : AppDialog(QObject::tr("About Pastes"), 492, parent)
{
	auto *hero = new QVBoxLayout;
	hero->setSpacing(8);
	auto *icon = new QLabel(this);
	icon->setFixedSize(72, 72);
	icon->setPixmap(QIcon(":/resources/pastes.svg").pixmap(QSize(72, 72), devicePixelRatioF()));
	hero->addWidget(icon, 0, Qt::AlignHCenter);
	auto *name = new QLabel(QStringLiteral("Pastes"), this);
	name->setObjectName("AboutBrand");
	hero->addWidget(name, 0, Qt::AlignHCenter);
	auto *version = new RoundedLabel(QObject::tr("Version %1").arg(QStringLiteral(PASTES_VERSION)), RoundedRole::HistoryBadge, this);
	version->setObjectName("AboutVersion");
	hero->addWidget(version, 0, Qt::AlignHCenter);
	auto *tagline = new QLabel(QObject::tr("Keep useful copies close at hand."), this);
	tagline->setObjectName("AppDialogMuted");
	tagline->setWordWrap(true);
	tagline->setAlignment(Qt::AlignCenter);
	hero->addSpacing(4);
	hero->addWidget(tagline);
	bodyLayout()->addLayout(hero);
	auto *details = new RoundedWidget(RoundedRole::PreviewContent, this);
	details->setObjectName("AppDialogCard");
	auto *rows = new QVBoxLayout(details);
	rows->setContentsMargins(18, 16, 18, 16);
	rows->setSpacing(12);
	auto addRow = [&](const QString &label, const QString &value) {
		auto *row = new QHBoxLayout;
		auto *caption = new QLabel(label, details);
		caption->setObjectName("AppDialogMuted");
		auto *text = new QLabel(value, details);
		text->setObjectName("AppDialogValue");
		row->addWidget(caption); row->addStretch(); row->addWidget(text);
		rows->addLayout(row);
	};
	addRow(QObject::tr("Created by"), QStringLiteral("Jackie Liu"));
	addRow(QObject::tr("License"), QStringLiteral("LGPL v3"));
	addRow(QObject::tr("History"), QObject::tr("Stored locally · Last %1 days").arg(HistoryPolicy::retentionDays));
	bodyLayout()->addWidget(details);
	auto *project = new RoundedButton(this);
	project->setObjectName("PreviewAction");
	project->setText(QObject::tr("Project page"));
	project->setAutoDefault(false);
	QObject::connect(project, &QPushButton::clicked, this, [](void) {
		QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/JackieLiu1/pastes")));
	});
	auto *done = new RoundedButton(this);
	done->setObjectName("PreviewAction");
	done->setProperty("primary", true);
	done->setText(QObject::tr("Done"));
	done->setDefault(true);
	QObject::connect(done, &QPushButton::clicked, this, &QDialog::accept);
	auto *footer = new QHBoxLayout;
	footer->setSpacing(8);
	footer->addWidget(project); footer->addStretch(); footer->addWidget(done);
	bodyLayout()->addLayout(footer);
	adjustSize();
}
