#include "appdialog.h"
#include "roundedwidgets.h"

#include <QApplication>
#include <QDesktopServices>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

AppDialog::AppDialog(const QString &title, int width, QWidget *parent) :
	QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint),
	m_surface(new RoundedWidget(RoundedRole::Preview, this)),
	m_header(new QWidget(m_surface)), m_subtitle(new QLabel(m_header)),
	m_body(new QVBoxLayout)
{
	setObjectName("AppDialog");
	setWindowTitle(title);
	setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_MACOS
	setWindowFlag(Qt::NoDropShadowWindowHint);
#endif
	setModal(true);
	setFixedWidth(width);
	setFont(QFont(QStringLiteral("Segoe UI"), 10));
	auto *outer = new QVBoxLayout(this);
#ifdef Q_OS_MACOS
	/* Paint the rounded surface at the native window edge, without a
	 * transparent shadow gutter or a second AppKit outline. */
	outer->setContentsMargins(0, 0, 0, 0);
#else
	outer->setContentsMargins(14, 14, 14, 18);
#endif
	outer->addWidget(m_surface);
	m_surface->setObjectName("AppDialogSurface");
#ifndef Q_OS_MACOS
	auto *shadow = new QGraphicsDropShadowEffect(m_surface);
	shadow->setOffset(0, 4);
	shadow->setBlurRadius(24);
	shadow->setColor(qApp->property("pastesDark").toBool() ? QColor(0, 0, 0, 75) : QColor(12, 30, 23, 45));
	m_surface->setGraphicsEffect(shadow);
#endif
	auto *layout = new QVBoxLayout(m_surface);
	layout->setContentsMargins(24, 20, 24, 24);
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
	auto *close = new RoundedButton(m_header);
	close->setObjectName("PreviewClose");
	close->setText(QStringLiteral("×"));
	close->setFixedSize(32, 32);
	close->setToolTip(QObject::tr("Close (Esc)"));
	close->setAccessibleName(QObject::tr("Close"));
	close->setAutoDefault(false);
	QObject::connect(close, &QPushButton::clicked, this, &QDialog::reject);
	auto *header = new QHBoxLayout(m_header);
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(14);
	header->addLayout(titles, 1);
	header->addWidget(close, 0, Qt::AlignTop);
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
	if (object == m_header && event->type() == QEvent::MouseButtonPress) {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton && windowHandle()) {
			windowHandle()->startSystemMove();
			return true;
		}
	}
	return QDialog::eventFilter(object, event);
}

void AppDialog::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	QScreen *screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
	if (!screen) return;
	const QRect area = screen->availableGeometry();
	move(area.center()-QPoint(width()/2, height()/2));
}

void AppDialog::changeEvent(QEvent *event)
{
	QDialog::changeEvent(event);
	if (event->type() == QEvent::StyleChange) {
		if (auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(m_surface->graphicsEffect()))
			shadow->setColor(qApp->property("pastesDark").toBool() ? QColor(0, 0, 0, 75) : QColor(12, 30, 23, 45));
	}
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
	addRow(QObject::tr("History"), QObject::tr("Stored locally · Last 7 days"));
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
