#include "tests/testsupport.h"
#include "platform/windowintegration.h"
#include "ui/roundedwidgets.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QPainter>
#include <QScreen>
#include <windows.h>

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
		QStringLiteral("reopen"), QStringLiteral("resize")}) {
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
			HRGN region = CreateRectRgn(0, 0, 0, 0);
			const HWND window = reinterpret_cast<HWND>(panel.winId());
			const int result = GetWindowRgn(window, region);
			const bool topClipped = !PtInRegion(region, 0, 0);
			RECT rect{};
			GetClientRect(window, &rect);
			const bool bottomSquare = PtInRegion(region, 0, rect.bottom-1);
			DeleteObject(region);
			require(result != ERROR && topClipped && bottomSquare, "Native backdrop lost its top-only corner mask");
		} else require(!panel.property("pastesPanelBackdrop").toBool(), "Non-native backend enabled Windows glass");
		if (!capture) continue;
		require(panel.property("pastesPanelBackdrop").toBool(), "Native backdrop could not be enabled");
		const auto area = background.frameGeometry();
		const QImage image = background.screen()->grabWindow(0, area.x(), area.y(),
			area.width(), area.height()).toImage();
		require(!image.isNull(), "Native desktop capture failed");
		QDir().mkpath("build/backdrop-preview");
		const qreal ratio = image.devicePixelRatio();
		const QString suffix = qFuzzyCompare(ratio, qreal(1)) ? QString() : "-dpi-"+QString::number(ratio);
		require(image.save("build/backdrop-preview/panel-"+stage+suffix+".png"), "Could not save native backdrop preview");
		const QColor left = image.pixelColor(qRound(340*ratio), qRound(270*ratio));
		const QColor right = image.pixelColor(qRound(740*ratio), qRound(270*ratio));
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
	return runTest("Windows backdrop lifecycle and native corner clipping", [=] {
		nativeBackdrop(capture);
	}) ? 1 : 0;
}
