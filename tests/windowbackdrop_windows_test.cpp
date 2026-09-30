#include "tests/testsupport.h"
#include "platform/windowintegration.h"
#include "ui/roundedwidgets.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <QtMath>
#include <windows.h>
#include <dwmapi.h>

namespace {
class Background final : public QWidget
{
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
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
	const QPoint &offset)
{
	const qreal ratio = actual.devicePixelRatio();
	QImage contour(QSize(qCeil(panel.width()*ratio), qCeil(panel.height()*ratio)),
		QImage::Format_ARGB32_Premultiplied);
	contour.setDevicePixelRatio(ratio);
	contour.fill(Qt::transparent);
	QPainter painter(&contour);
	RoundedSurface surface;
	surface.paint(&panel, RoundedRole::Panel, painter);
	painter.end();
	contour.save("build/backdrop-preview/corner-contour.png");
	const int extent = qCeil(20*ratio);
	int checked = 0, maximumDifference = 0;
	for (int side : {0, 1}) {
		for (int y = 0; y < extent; ++y) {
			const auto *mask = reinterpret_cast<const QRgb *>(contour.constScanLine(y));
			const auto *result = reinterpret_cast<const QRgb *>(actual.constScanLine(offset.y()+y));
			const auto *original = reinterpret_cast<const QRgb *>(background.constScanLine(offset.y()+y));
			for (int column = 0; column < extent; ++column) {
				const int x = side ? contour.width()-1-column : column;
				if (qAlpha(mask[x])) continue;
				/* Exclude the one-pixel AA fringe; DWM and Qt use different filters. */
				bool clear = true;
				for (int neighborY = qMax(0, y-1); neighborY <= y+1; ++neighborY) {
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
	require(checked >= 2, "Corner comparison did not inspect both transparent corners");
	/* A small difference permits the native shadow. An acrylic plate changes
	 * these patterned background pixels by tens or hundreds of RGB levels. */
	require(maximumDifference <= 16, "Native acrylic leaked outside the painted corner contour");
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
	Platform::initializePanel(&panel);
	panel.setAttribute(Qt::WA_TranslucentBackground);
	panel.setAttribute(Qt::WA_DontShowOnScreen, !capture);
	panel.setGeometry(120, 150, 860, 310);
	RoundedWidget card(RoundedRole::Card, &panel);
	card.setGeometry(25, 85, 240, 150);
	QLabel title("Pastes | Windows Acrylic", &panel);
	title.setGeometry(20, 20, 720, 40);
	QLabel content("Card contents stay clear", &card);
	content.setGeometry(20, 20, 210, 40);
	title.setFont(QFont("Segoe UI", 17));
	content.setFont(QFont("Segoe UI", 11));
	for (const QString &stage : {QStringLiteral("dark"), QStringLiteral("light"),
		QStringLiteral("reopen"), QStringLiteral("resize"), QStringLiteral("docked")}) {
		QImage baseline;
		if (capture) {
			panel.hide();
			if (stage == "docked") {
				const QRect area = background.screen()->availableGeometry();
				background.setGeometry(area.x(), area.bottom()-359, area.width(), 360);
				panel.setGeometry(area.x(), area.bottom()-309, area.width(), 310);
			}
			background.raise();
			settle();
			baseline = captureBackground(background);
		}
		const bool dark = stage != "light";
		qApp->setProperty("pastesDark", dark);
		title.setStyleSheet(dark ? "color: white" : "color: #181818");
		content.setStyleSheet(dark ? "color: white" : "color: #181818");
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
					&corners, sizeof(corners))) && corners == DWMWCP_ROUND,
					"Native backdrop did not request matching system corners");
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
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	const bool capture = app.arguments().contains("--capture");
	return runTest("Windows backdrop lifecycle and visible corner clipping", [=] {
		nativeBackdrop(capture);
	}) ? 1 : 0;
}
