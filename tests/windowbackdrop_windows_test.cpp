#include "tests/testsupport.h"
#include "tests/colorcontrast.h"
#include "platform/windowintegration.h"
#include "ui/roundedwidgets.h"
#include "ui/appdialog.h"
#include "ui/settingsdialog.h"
#include "ui/previewdialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QTemporaryDir>
#include <memory>
#include <QtMath>
#include <windows.h>
#include <dwmapi.h>

namespace {
class Background final : public QWidget
{
	bool m_extremes = false;
	bool m_bottomBand = true;
public:
	void setExtremes(bool extremes, bool bottomBand = true)
	{ m_extremes = extremes; m_bottomBand = bottomBand; update(); }
private:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		if (m_extremes) {
			painter.fillRect(rect(), Qt::black);
			painter.fillRect(QRect(width()/2, 0, width()/2, height()), Qt::white);
			/* A contrasting band keeps both bottom corners observable even
			 * when the light material nearly matches the white background. */
			if (m_bottomBand) painter.fillRect(QRect(0, 330, width(), 40), Qt::black);
			return;
		}
		painter.fillRect(rect(), QColor("#159BCD"));
		painter.fillRect(QRect(width()/2, 0, width()/2, height()), QColor("#DF6944"));
		for (int x = 0; x < width(); x += 12)
			painter.fillRect(QRect(x, 0, 6, height()), QColor(255, 255, 255, 145));
	}
};

void settle(void)
{
	QEventLoop loop;
	QTimer::singleShot(450, &loop, &QEventLoop::quit);
	loop.exec();
}

QImage captureBackground(Background &background)
{
	const auto area = background.frameGeometry();
	return background.screen()->grabWindow(0, area.x(), area.y(), area.width(), area.height()).toImage();
}

void verifyCornerPixels(const QImage &actual, const QImage &background, QWidget &panel,
	const QPoint &offset, RoundedRole role = RoundedRole::Panel)
{
	const qreal ratio = actual.devicePixelRatio();
	QImage expected = background;
	if (role == RoundedRole::Preview) {
		/* Dialog shadows belong to Qt, outside the clipped glass. Compare
		 * against the same Qt foreground over the unobscured background. */
		QWidget *window = panel.window();
		const QPoint inset = panel.mapTo(window, QPoint());
		QPainter shadow(&expected);
		shadow.drawPixmap(QPointF(offset.x()/ratio-inset.x(), offset.y()/ratio-inset.y()), window->grab());
	}
	QImage contour(QSize(qCeil(panel.width()*ratio), qCeil(panel.height()*ratio)),
		QImage::Format_ARGB32_Premultiplied);
	contour.setDevicePixelRatio(ratio);
	contour.fill(Qt::transparent);
	QPainter painter(&contour);
	RoundedSurface surface;
	surface.paint(&panel, role, painter);
	painter.end();
	contour.save("build/backdrop-preview/corner-contour.png");
	const int extent = qCeil((Platform::panelAppearance().cornerRadius+2)*ratio);
	int checked = 0, maximumDifference = 0;
	for (int corner : {0, 1, 2, 3}) {
		if (corner >= 2 && role == RoundedRole::Panel) continue;
		const bool side = corner%2, bottom = corner >= 2;
		for (int y = 0; y < extent; ++y) {
			const int rowY = bottom ? contour.height()-1-y : y;
			const auto *mask = reinterpret_cast<const QRgb *>(contour.constScanLine(rowY));
			const auto *result = reinterpret_cast<const QRgb *>(actual.constScanLine(offset.y()+rowY));
			const auto *original = reinterpret_cast<const QRgb *>(expected.constScanLine(offset.y()+rowY));
			for (int column = 0; column < extent; ++column) {
				const int x = side ? contour.width()-1-column : column;
				if (qAlpha(mask[x])) continue;
				/* Exclude the one-pixel AA fringe; DWM and Qt use different filters. */
				bool clear = true;
				for (int neighborY = qMax(0, rowY-1); neighborY <= qMin(contour.height()-1, rowY+1); ++neighborY) {
					const auto *neighbor = reinterpret_cast<const QRgb *>(contour.constScanLine(neighborY));
					for (int neighborX = qMax(0, x-1); neighborX <= qMin(contour.width()-1, x+1); ++neighborX)
						if (qAlpha(neighbor[neighborX])) clear = false;
				}
				if (!clear) continue;
				const QRgb first = result[offset.x()+x], second = original[offset.x()+x];
				const int difference = qMax(qAbs(qRed(first)-qRed(second)),
					qMax(qAbs(qGreen(first)-qGreen(second)), qAbs(qBlue(first)-qBlue(second))));
				maximumDifference = qMax(maximumDifference, difference);
				++checked;
			}
		}
	}
	qInfo() << "Outside corner pixels:" << checked << "maximum RGB difference:" << maximumDifference;
	const qreal radius = Platform::panelAppearance().cornerRadius*ratio;
	require(checked >= radius*radius/10, "Corner comparison did not inspect both custom corners");
	/* A small difference permits the native shadow. An acrylic plate changes
	 * these patterned background pixels by tens or hundreds of RGB levels. */
	require(maximumDifference <= 16, "Native acrylic leaked outside the painted corner contour");
	if (role != RoundedRole::Panel) return;
	for (int side : {0, 1}) {
		const int x = side ? contour.width()-1-qRound(2*ratio) : qRound(2*ratio);
		const int y = contour.height()-1-qRound(2*ratio);
		const QColor first = actual.pixelColor(offset.x()+x, offset.y()+y);
		const QColor second = background.pixelColor(offset.x()+x, offset.y()+y);
		const int difference = qMax(qAbs(first.red()-second.red()),
			qMax(qAbs(first.green()-second.green()), qAbs(first.blue()-second.blue())));
		require(difference > 16, "A native bottom corner was rounded away");
	}
}

void nativeBackdrop(bool capture)
{
	const bool native = QGuiApplication::platformName() == "windows";
	Background background;
	background.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	background.setGeometry(100, 100, 900, 400);
	background.setAttribute(Qt::WA_DontShowOnScreen, !capture);
	if (capture) background.show();
	RoundedWidget panel(RoundedRole::Panel);
	panel.setObjectName("MainFrame");
	Platform::initializePanel(&panel);
	panel.setAttribute(Qt::WA_TranslucentBackground);
	panel.setAttribute(Qt::WA_DontShowOnScreen, !capture);
	panel.setGeometry(120, 150, 860, 310);
	RoundedWidget card(RoundedRole::Card, &panel);
	card.setObjectName("PasteItemFrame");
	card.setGeometry(25, 85, 240, 150);
	QLabel title("Pastes", &panel);
	title.setObjectName("BrandTitle");
	title.setGeometry(20, 20, 110, 40);
	QLabel tab("Clipboard history", &panel);
	tab.setObjectName("HistoryTab");
	tab.setGeometry(145, 20, 260, 40);
	QLabel paused("Recording paused", &panel);
	paused.setObjectName("RecordingStatus");
	paused.setGeometry(450, 20, 360, 40);
	QLabel count("78 records", &panel);
	count.setObjectName("HistoryCount");
	count.setGeometry(20, 270, 240, 25);
	QLabel hint("Enter: paste | Space: preview", &panel);
	hint.setObjectName("KeyboardHint");
	hint.setGeometry(300, 270, 400, 25);
	QLabel content("Card contents stay clear", &card);
	content.setGeometry(20, 50, 210, 40);
	QWidget banner(&card);
	banner.setObjectName("Barnner");
	banner.setGeometry(0, 0, 240, 40);
	QLabel type("Text", &banner), time("1 hour ago", &banner);
	type.setObjectName("CardType");
	time.setObjectName("CardTime");
	type.setGeometry(20, 10, 80, 20);
	time.setGeometry(130, 10, 100, 20);
	for (const QString &stage : {QStringLiteral("dark"), QStringLiteral("light"),
		QStringLiteral("light-contrast"), QStringLiteral("reopen"), QStringLiteral("resize"),
		QStringLiteral("docked")}) {
		QImage baseline;
		if (capture) {
			panel.hide();
			background.setExtremes(stage == "light-contrast");
			if (stage == "docked") {
				const QRect area = background.screen()->availableGeometry();
				background.setGeometry(area.x(), area.bottom()-359, area.width(), 360);
				panel.setGeometry(area.x(), area.bottom()-309, area.width(), 310);
			}
			background.raise();
			settle();
			const HWND owner = GetWindow(reinterpret_cast<HWND>(panel.winId()), GW_OWNER);
			if (owner) require(!IsWindowVisible(owner), "Hidden panel left its backdrop visible");
			baseline = captureBackground(background);
		}
		const bool dark = stage != "light" && stage != "light-contrast";
		qApp->setProperty("pastesDark", dark);
		QFile theme(dark ? ":/resources/theme-dark.qss" : ":/resources/theme-light.qss");
		require(theme.open(QFile::ReadOnly), "Native contrast test could not load the real theme");
		panel.setStyleSheet(QString::fromUtf8(theme.readAll()));
		if (stage == "reopen") panel.hide();
		if (stage == "resize") panel.resize(820, 300);
		Platform::preparePanel(&panel);
		panel.show();
		panel.raise();
		Platform::updatePanelBackdrop(&panel);
		settle();
		if (native) {
			const HWND window = reinterpret_cast<HWND>(panel.winId());
			if (panel.property("pastesPanelBackdrop").toBool()) {
				DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_DEFAULT;
				require(SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE,
					&corners, sizeof(corners))) && corners == DWMWCP_DONOTROUND,
					"System corners overrode the custom contour");
				const HWND backdrop = GetWindow(window, GW_OWNER);
				require(backdrop && IsWindow(backdrop), "Composition backdrop owner missing");
				const auto style = GetWindowLongPtrW(backdrop, GWL_EXSTYLE);
				require((style & (WS_EX_NOACTIVATE | WS_EX_TRANSPARENT)) ==
					(WS_EX_NOACTIVATE | WS_EX_TRANSPARENT), "Backdrop could intercept input or activation");
				require(SendMessageW(backdrop, WM_NCHITTEST, 0, 0) == HTTRANSPARENT,
					"Backdrop intercepted mouse hit testing");
			}
		} else require(!panel.property("pastesPanelBackdrop").toBool(), "Non-native backend enabled Windows glass");
		if (!capture) continue;
		require(panel.property("pastesPanelBackdrop").toBool(), "Native backdrop could not be enabled");
		const QImage image = captureBackground(background);
		require(!image.isNull(), "Native desktop capture failed");
		QDir().mkpath("build/backdrop-preview");
		const qreal ratio = image.devicePixelRatio();
		const QString suffix = qFuzzyCompare(ratio, qreal(1)) ? QString() : "-dpi-"+QString::number(ratio);
		require(image.save("build/backdrop-preview/panel-"+stage+suffix+".png"), "Could not save native backdrop preview");
		require(baseline.save("build/backdrop-preview/baseline-"+stage+suffix+".png"), "Could not save the corner baseline");
		const QPoint offset = panel.frameGeometry().topLeft()-background.frameGeometry().topLeft();
		verifyCornerPixels(image, baseline, panel, QPoint(qRound(offset.x()*ratio), qRound(offset.y()*ratio)));
		if (stage == "light-contrast") {
			const QColor left = image.pixelColor(qRound((offset.x()+320)*ratio), qRound((offset.y()+65)*ratio));
			const QColor right = image.pixelColor(qRound((offset.x()+700)*ratio), qRound((offset.y()+65)*ratio));
			qreal minimum = 100;
			for (QLabel *label : {&title, &tab, &paused, &count, &hint}) {
				const QColor ink = label->palette().color(QPalette::WindowText);
				minimum = qMin(minimum, qMin(contrastRatio(ink, left), contrastRatio(ink, right)));
			}
			const QColor cardColor = image.pixelColor(qRound((offset.x()+145)*ratio), qRound((offset.y()+215)*ratio));
			for (QLabel *label : {&type, &time, &content})
				minimum = qMin(minimum, contrastRatio(label->palette().color(QPalette::WindowText), cardColor));
			qInfo() << "Light text minimum contrast over black/white:" << minimum;
			require(minimum >= 4.5, "Light theme text was unreadable over an extreme native backdrop");
			require(right.red()-left.red() >= 64, "Light tint concealed the frosted backdrop");
			continue;
		}
		const QColor left = image.pixelColor(image.width()/3, qRound(270*ratio));
		const QColor right = image.pixelColor(image.width()*3/4, qRound(270*ratio));
		require(left.blue() > left.red()+4 && right.red() > right.blue()+4,
			"Native backdrop did not retain the background colors");
		int variation = 0;
		const auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(qRound(275*ratio)));
		for (int x = 320; x < 380; ++x) {
			const int first = qGray(row[qRound(x*ratio)]);
			const int second = qGray(row[qRound((x+1)*ratio)]);
			variation += qAbs(first-second);
		}
		require(variation < 180, "Backdrop stripes were transmitted without being blurred");
	}
}

void nativeDialogBackdrops(bool capture)
{
	const bool native = QGuiApplication::platformName() == "windows";
	Background background;
	background.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	background.setGeometry(QGuiApplication::primaryScreen()->availableGeometry());
	background.setAttribute(Qt::WA_DontShowOnScreen, !capture);
	if (capture) background.show();
	RoundedWidget parent(RoundedRole::Panel);
	Platform::initializePanel(&parent);
	parent.setAttribute(Qt::WA_TranslucentBackground);
	parent.setAttribute(Qt::WA_DontShowOnScreen, true);
	Platform::preparePanel(&parent);
	auto entry = textEntry("Preview contents remain readable over the glass surface.");
	for (const QString &kind : {QStringLiteral("about"), QStringLiteral("settings"), QStringLiteral("preview")}) {
		std::unique_ptr<QDialog> dialog;
		if (kind == "about") dialog = std::make_unique<AboutDialog>(&parent);
		else if (kind == "settings") dialog = std::make_unique<SettingsDialog>("Win+V", &parent);
		else dialog = std::make_unique<PreviewDialog>(*entry, &parent);
		dialog->setAttribute(Qt::WA_DontShowOnScreen, !capture);
		if (capture) dialog->setWindowFlag(Qt::WindowStaysOnTopHint);
		auto *surface = dialog->findChild<QWidget *>("AppDialogSurface");
		if (!surface) surface = dialog->findChild<QWidget *>("PreviewSurface");
		require(surface, "Native dialog surface missing");
		for (const QString &stage : {QStringLiteral("light"), QStringLiteral("light-contrast"),
			QStringLiteral("dark"), QStringLiteral("dark-contrast")}) {
			dialog->hide();
			background.setExtremes(stage.endsWith("-contrast"), false);
			if (capture) background.raise();
			settle();
			QImage baseline;
			if (capture) baseline = captureBackground(background);
			const bool dark = stage.startsWith("dark");
			qApp->setProperty("pastesDark", dark);
			QFile theme(dark ? ":/resources/theme-dark.qss" : ":/resources/theme-light.qss");
			require(theme.open(QFile::ReadOnly), "Native dialog theme missing");
			dialog->setStyleSheet(QString::fromUtf8(theme.readAll()));
			dialog->show();
			dialog->raise();
			settle();
			if (!native) {
				require(!dialog->property("pastesDialogBackdrop").toBool(), "Non-native dialog enabled Windows glass");
				continue;
			}
			if (!capture) continue;
			require(dialog->property("pastesDialogBackdrop").toBool(), "Native dialog backdrop was not enabled");
			const HWND window = reinterpret_cast<HWND>(dialog->winId());
			const HWND helper = GetWindow(window, GW_OWNER);
			require(helper && IsWindowVisible(helper), "Dialog backdrop is missing or hidden");
			require(SendMessageW(helper, WM_NCHITTEST, 0, 0) == HTTRANSPARENT, "Dialog backdrop intercepted input");
			const qreal ratio = dialog->devicePixelRatioF();
			const QPoint global = surface->mapToGlobal(QPoint());
			RECT bounds{}; GetWindowRect(helper, &bounds);
			require(qAbs(bounds.left-qRound(global.x()*ratio)) <= 1 &&
				qAbs(bounds.top-qRound(global.y()*ratio)) <= 1 &&
				bounds.right-bounds.left == qRound(surface->width()*ratio) &&
				bounds.bottom-bounds.top == qRound(surface->height()*ratio),
				"Dialog glass did not follow the surface and shadow gutters");
			const QImage actual = captureBackground(background);
			const QPoint offset = global-background.frameGeometry().topLeft();
			const QPoint pixels(qRound(offset.x()*ratio), qRound(offset.y()*ratio));
			verifyCornerPixels(actual, baseline, *surface, pixels, RoundedRole::Preview);
			const QString suffix = qFuzzyCompare(ratio, qreal(1)) ? QString() : "-dpi-"+QString::number(ratio);
			const QRect crop(pixels-QPoint(qRound(14*ratio), qRound(14*ratio)),
				QSize(qRound((surface->width()+28)*ratio), qRound((surface->height()+28)*ratio)));
			require(actual.copy(crop).save("build/backdrop-preview/dialog-"+kind+"-"+stage+suffix+".png"),
				"Could not save the native dialog preview");
			const QPoint first(pixels.x()+qRound(12*ratio), pixels.y()+qRound(40*ratio));
			const QPoint second(pixels.x()+qRound((surface->width()-12)*ratio), first.y());
			const QColor left = actual.pixelColor(first), right = actual.pixelColor(second);
			if (stage.endsWith("-contrast")) {
				qreal minimum = 100;
				for (QLabel *label : dialog->findChildren<QLabel *>()) {
					if (label->text().isEmpty() || !label->isVisibleTo(dialog.get())) continue;
					bool opaque = label->objectName() == "AboutVersion";
					for (QWidget *ancestor = label->parentWidget(); ancestor && ancestor != surface; ancestor = ancestor->parentWidget())
						if (ancestor->objectName() == "AppDialogCard" || ancestor->objectName() == "PreviewContent") opaque = true;
					if (opaque) continue;
					const QColor ink = label->palette().color(QPalette::WindowText);
					minimum = qMin(minimum, qMin(contrastRatio(ink, left), contrastRatio(ink, right)));
				}
				qInfo() << kind << stage << "glass text minimum contrast:" << minimum
					<< "background transmission:" << right.red()-left.red();
				require(minimum >= 4.5, "Native dialog glass made text unreadable");
				require(right.red()-left.red() >= (dark ? 48 : 64), "Native dialog tint concealed the glass");
			} else {
				require(left.blue() > left.red()+4 && right.red() > right.blue()+4,
					"Dialog glass lost background colors");
				int variation = 0, unblurred = 0;
				const auto *row = reinterpret_cast<const QRgb *>(actual.constScanLine(first.y()));
				const auto *source = reinterpret_cast<const QRgb *>(baseline.constScanLine(first.y()));
				for (int x = pixels.x()+qRound(6*ratio); x < pixels.x()+qRound(20*ratio); ++x) {
					variation += qAbs(qGray(row[x])-qGray(row[x+1]));
					unblurred += qAbs(qGray(source[x])-qGray(source[x+1]));
				}
				require(unblurred > 30 && variation < unblurred/4, "Dialog background was transparent without blur");
			}
			dialog->move(dialog->pos()+QPoint(3, 3));
			dialog->setFixedHeight(dialog->height()+2);
			settle();
			GetWindowRect(helper, &bounds);
			const QPoint moved = surface->mapToGlobal(QPoint());
			require(qAbs(bounds.left-qRound(moved.x()*ratio)) <= 1 &&
				qAbs(bounds.top-qRound(moved.y()*ratio)) <= 1 &&
				bounds.bottom-bounds.top == qRound(surface->height()*ratio), "Dialog glass stopped following a move or resize");
			dialog->hide();
			settle();
			require(!IsWindowVisible(helper), "Closing a dialog left its glass visible");
		}
	}
}
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	QTemporaryDir preferences;
	QCoreApplication::setOrganizationName("PastesBackdropTests");
	QCoreApplication::setApplicationName("Dialogs");
	QSettings::setDefaultFormat(QSettings::IniFormat);
	QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
	const bool capture = app.arguments().contains("--capture");
	return runTest("Windows backdrop lifecycle and visible corner clipping", [=] {
		nativeBackdrop(capture);
		nativeDialogBackdrops(capture);
		int backdrops = 0;
		EnumWindows([](HWND window, LPARAM count) -> BOOL {
			DWORD process = 0;
			GetWindowThreadProcessId(window, &process);
			if (process != GetCurrentProcessId()) return TRUE;
			wchar_t name[64]{};
			GetClassNameW(window, name, 64);
			if (wcscmp(name, L"PastesCompositionBackdrop") == 0)
				++*reinterpret_cast<int *>(count);
			return TRUE;
		}, reinterpret_cast<LPARAM>(&backdrops));
		require(backdrops == 0, "Panel destruction left a native backdrop window alive");
	}) ? 1 : 0;
}
