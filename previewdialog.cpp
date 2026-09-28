#include "previewdialog.h"
#include "pasteitem.h"
#include "roundedwidgets.h"
#include "platform/windowintegration.h"

#include <QApplication>
#include <QDateTime>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScreen>
#include <QScrollBar>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>

/* A preview owns its display values, never a pointer that the database
 * worker may release while the modal window is open. */
class PreviewImage : public QWidget
{
public:
	PreviewImage(const QImage &image, QWidget *parent) : QWidget(parent), m_image(image)
	{
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
		setAccessibleName(QObject::tr("Image preview"));
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		if (m_image.isNull()) return;
		const qreal ratio = devicePixelRatioF();
		const QSize available = size()-QSize(24, 24);
		if (available.isEmpty()) return;
		if (m_cached_size != available || m_cached_ratio != ratio) {
			m_cached_size = available;
			m_cached_ratio = ratio;
			QSize target = m_image.size();
			target.scale(QSize(qCeil(available.width()*ratio), qCeil(available.height()*ratio)),
				Qt::KeepAspectRatio);
			m_pixmap = QPixmap::fromImage(m_image.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			m_pixmap.setDevicePixelRatio(ratio);
		}
		QPainter painter(this);
		painter.drawPixmap(QPointF((width()-m_pixmap.width()/ratio)/2,
			(height()-m_pixmap.height()/ratio)/2), m_pixmap);
	}

private:
	QImage m_image;
	QPixmap m_pixmap;
	QSize m_cached_size;
	qreal m_cached_ratio = 0;
};

PreviewDialog::PreviewDialog(const ItemData &data, QWidget *parent) :
	QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint),
	m_surface(new RoundedWidget(RoundedRole::Preview, this)),
	m_header(new QWidget(m_surface)), m_detail(new QLabel(m_surface))
{
	Platform::initializePreview(this);
	setObjectName("PreviewDialog");
	setWindowTitle(QObject::tr("Preview"));
	setAttribute(Qt::WA_TranslucentBackground);
	setModal(true);
	if (parent) parent->installEventFilter(this);
	const auto &appearance = Platform::dialogAppearance();
	QVBoxLayout *outer = new QVBoxLayout(this);
	outer->setContentsMargins(appearance.outerMargins);
	outer->addWidget(m_surface);
	if (!appearance.nativeControls) {
		auto *shadow = new QGraphicsDropShadowEffect(m_surface);
		shadow->setOffset(0, 4);
		shadow->setBlurRadius(24);
		shadow->setColor(qApp->property("pastesDark").toBool() ? QColor(0, 0, 0, 75) : QColor(12, 30, 23, 45));
		m_surface->setGraphicsEffect(shadow);
	}
	m_surface->setObjectName("PreviewSurface");
	QVBoxLayout *layout = new QVBoxLayout(m_surface);
	layout->setContentsMargins(appearance.nativeControls ? appearance.contentMargins :
		QMargins(20, 16, 20, 16));
	layout->setSpacing(16);

	QLabel *icon = new QLabel(m_header);
	icon->setFixedSize(30, 30);
	icon->setAlignment(Qt::AlignCenter);
	if (!data.icon.isNull()) {
		const qreal ratio = devicePixelRatioF();
		const int pixels = qRound(24*ratio);
		QPixmap pixmap = QPixmap::fromImage(data.icon).scaled(pixels, pixels,
			Qt::KeepAspectRatio, Qt::SmoothTransformation);
		pixmap.setDevicePixelRatio(ratio);
		icon->setPixmap(pixmap);
	} else
		icon->setPixmap(QIcon(":/resources/pastes.svg").pixmap(QSize(26, 26), devicePixelRatioF()));
	QLabel *title = new QLabel(QObject::tr("Preview"), m_header);
	title->setObjectName("PreviewTitle");
	QLabel *time = new QLabel(data.time.toString("yyyy-MM-dd  HH:mm"), m_header);
	time->setObjectName("PreviewMeta");
	QVBoxLayout *titles = new QVBoxLayout;
	titles->setSpacing(3);
	titles->addWidget(title);
	titles->addWidget(time);
	RoundedButton *close = nullptr;
	if (!appearance.nativeControls) {
		close = new RoundedButton(m_header);
		close->setObjectName("PreviewClose");
		close->setText(QStringLiteral("×"));
		close->setFixedSize(32, 32);
		close->setToolTip(QObject::tr("Close (Esc)"));
		close->setAccessibleName(QObject::tr("Close"));
		close->setAutoDefault(false);
		QObject::connect(close, &QPushButton::clicked, this, &QDialog::reject);
	}
	QHBoxLayout *header = new QHBoxLayout(m_header);
	header->setContentsMargins(0, 0, 0, 0);
	header->setSpacing(10);
	header->addWidget(icon);
	header->addLayout(titles);
	header->addStretch();
	if (close) header->addWidget(close);
	this->installEventFilter(this);
	m_surface->installEventFilter(this);
	m_header->installEventFilter(this);
	for (QLabel *label : m_header->findChildren<QLabel *>())
		label->setAttribute(Qt::WA_TransparentForMouseEvents);
	layout->addWidget(m_header);

	RoundedWidget *content = new RoundedWidget(RoundedRole::PreviewContent, m_surface);
	content->setObjectName("PreviewContent");
	QVBoxLayout *body = new QVBoxLayout(content);
	body->setContentsMargins(8, 8, 8, 8);
	const QMimeData *mime = data.mimeData;
	if (mime->hasImage() && !mime->hasUrls() && !(mime->hasHtml() && !mime->text().trimmed().isEmpty())) {
		const QImage image = qvariant_cast<QImage>(mime->imageData());
		body->addWidget(new PreviewImage(image, content));
		m_detail->setText(QString("%1 × %2 px").arg(image.width()).arg(image.height()));
	} else {
		QPlainTextEdit *text = new QPlainTextEdit(content);
		text->setObjectName("PreviewText");
		text->setFrameShape(QFrame::NoFrame);
		text->verticalScrollBar()->setObjectName("PreviewScroll");
		text->verticalScrollBar()->style()->unpolish(text->verticalScrollBar());
		text->verticalScrollBar()->style()->polish(text->verticalScrollBar());
		text->setReadOnly(true);
		text->setAccessibleName(QObject::tr("Preview"));
		QString value = mime->text();
		if (mime->hasUrls()) {
			QStringList paths;
			for (const QUrl &url : mime->urls())
				paths.append(url.isLocalFile() ? url.toLocalFile() : url.toString());
			value = paths.join('\n');
			m_detail->setText(QObject::tr("%1 files").arg(paths.count()));
		} else {
			m_detail->setText(QObject::tr("%1 characters").arg(value.size()));
		}
		text->setPlainText(value);
		body->addWidget(text);
	}
	layout->addWidget(content, 1);
	m_detail->setObjectName("PreviewMeta");
	RoundedButton *copy = new RoundedButton(m_surface);
	copy->setObjectName("PreviewAction");
	copy->setText(QObject::tr("Copy to Clipboard"));
	copy->setAutoDefault(false);
	QObject::connect(copy, &QPushButton::clicked, this, &PreviewDialog::copyRequested);
	RoundedButton *paste = new RoundedButton(m_surface);
	paste->setObjectName("PreviewAction");
	paste->setProperty("primary", true);
	paste->setText(QObject::tr("Paste"));
	paste->setAutoDefault(false);
	QObject::connect(paste, &QPushButton::clicked, this, &QDialog::accept);
	QHBoxLayout *footer = new QHBoxLayout;
	footer->setSpacing(8);
	footer->addWidget(m_detail);
	footer->addStretch();
	footer->addWidget(copy);
	footer->addWidget(paste);
	layout->addLayout(footer);

	auto *pasteShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
	QObject::connect(pasteShortcut, &QShortcut::activated, this, &QDialog::accept);
	if (mime->hasText()) {
		auto *plain = new QShortcut(QKeySequence("Shift+Return"), this);
		QObject::connect(plain, &QShortcut::activated, this, [this](void) {
			m_plain_text = true;
			accept();
		});
	}
	auto *space = new QShortcut(QKeySequence(Qt::Key_Space), this);
	QObject::connect(space, &QShortcut::activated, this, &QDialog::reject);
	if (close) setTabOrder(close, copy);
	setTabOrder(copy, paste);
}

bool PreviewDialog::eventFilter(QObject *object, QEvent *event)
{
	/* A workspace switch must also end the preview's modal event loop. */
	if (object == parentWidget() && event->type() == QEvent::Hide)
		reject();
	if ((object == this || object == m_surface || object == m_header) &&
		event->type() == QEvent::MouseButtonPress) {
		QMouseEvent *mouse = static_cast<QMouseEvent *>(event);
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

void PreviewDialog::showEvent(QShowEvent *event)
{
	Platform::preparePreview(this);
	QDialog::showEvent(event);
	QScreen *screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
	if (!screen) return;
	const QRect area = screen->availableGeometry();
	const QSize size(qMin(760, area.width()-32), qMin(560, area.height()-32));
	setFixedSize(size);
	move(area.center()-QPoint(width()/2, height()/2));
	QTimer::singleShot(0, this, [this](void) {
		if (isVisible()) Platform::activatePreview(this);
	});
}

void PreviewDialog::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Enter) {
		m_plain_text = event->modifiers().testFlag(Qt::ShiftModifier);
		accept();
		return;
	}
	QDialog::keyPressEvent(event);
}
