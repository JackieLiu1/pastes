#ifndef PLATFORM_WINDOWINTEGRATION_H
#define PLATFORM_WINDOWINTEGRATION_H

#include <QMargins>
#include <QRect>
#include <QSize>
#include <Qt>
#include <functional>

class QWidget;
class QScreen;

namespace Platform {

struct PanelAppearance
{
	QMargins margins{24, 16, 24, 12};
	int spacing = 10;
	qreal cornerRadius = 18;
	bool shadow = true;
	bool nativeBackdrop = false;
};

struct DialogAppearance
{
	Qt::WindowFlags flags = Qt::Dialog | Qt::FramelessWindowHint;
	QMargins outerMargins{14, 14, 14, 18};
	QMargins contentMargins{24, 20, 24, 24};
	QMargins previewContentMargins{20, 16, 20, 16};
	int previewSpacing = 16;
	bool nativeControls = false;
	Qt::ScrollBarPolicy scrollBar = Qt::ScrollBarAsNeeded;
};

const PanelAppearance &panelAppearance(void);
const DialogAppearance &dialogAppearance(void);
/* QWidget::isAncestorOf stops at top-level window boundaries. */
bool isOwnedWindow(QWidget *owner, QWidget *window);
/* An explicit screen is used during construction; later calls select the
 * destination display before the panel takes focus. */
QRect panelGeometry(QScreen *screen = nullptr);
QSize cardSize(const QSize &panelSize);
void initializePanel(QWidget *widget);
void enablePanelBlur(QWidget *widget);
void preparePanel(QWidget *widget);
void updatePanelBackdrop(QWidget *widget);
void activatePanel(QWidget *widget);
/* Dismiss with animation on focus loss, immediately after a Space change.
 * The observer belongs to the widget and expires with it. */
void watchPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss);
/* Fallback for backends without a native focus observer. */
void watchQtPanelDismissal(QWidget *widget, const std::function<void(bool)> &dismiss);
void initializeDialog(QWidget *widget);
void prepareDialog(QWidget *widget);
/* Clip native glass to the dialog's rounded surface, excluding shadow gutters.
 * The backend owns the observer and follows later moves, resizes and hides. */
void updateDialogBackdrop(QWidget *widget, QWidget *surface);
/* Item previews keep keyboard focus in the panel's current workspace. */
void initializePreview(QWidget *widget);
void preparePreview(QWidget *widget);
void activatePreview(QWidget *widget);
/* Input-transparent drag snapshots share the panel's stacking layer. */
void initializeDragOverlay(QWidget *widget);

}
#endif
