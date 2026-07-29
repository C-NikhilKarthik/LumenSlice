// One tri-axis slice pane (Axial / Coronal / Sagittal).
//
// Renders a slice from the bridge through window/level, blends the colored
// segmentation mask, draws crosshair lines at the shared focus voxel, and turns
// mouse input into high-level intents (scroll, focus, window/level, tool strokes)
// that MainWindow applies against the C core. The view holds no volume ownership —
// it reads the borrowed handle and shared settings from a ViewState.
#pragma once

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>

#include "ViewState.h"

namespace lumenwin {

class SliceView : public QWidget {
    Q_OBJECT
public:
    SliceView(int axis, ViewState* state, QWidget* parent = nullptr);

    int axis() const { return axis_; }
    // Reset zoom/pan back to fit-to-pane (e.g. when a new volume loads).
    void resetZoom();

signals:
    // Mouse wheel changed this axis' slice index (already clamped).
    void sliceScrolled(int axis, int newIndex);
    // Relative window/level nudge from a left/right drag on the image.
    void windowLevelDragged(float deltaLevel, float deltaWindow);
    // The user picked a focus voxel (Ctrl+click / middle click, or plain click
    // when no segmentation tool is active).
    void focusPicked(int x, int y, int z);
    // Segmentation intents, in slice-pixel coordinates on this axis/index.
    void paintStroke(int axis, int index, int cx, int cy, int radius, bool add);
    void floodClicked(int x, int y, int z);       // region-grow seed (voxel space)
    void levelTraceClicked(int axis, int index, int cx, int cy);
    void markupPointPicked(int x, int y, int z);   // markup placement (voxel space)
    void doubleClicked();                          // toggle pane maximize
    // A stroke/gesture that mutated the mask ended — good point to snapshot undo.
    void strokeBegan();
    // Shift + hover (no button) moved the shared crosshair to this voxel. Drives
    // the linked panes and the live 3D-view marker.
    void crosshairMoved(int x, int y, int z);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    // Aspect-preserving fit rect for the slice image (zoom-independent base).
    QRect baseRect(int imgW, int imgH) const;
    // The base rect after applying this pane's zoom + pan — the rect actually
    // drawn into and hit-tested against.
    QRect imageRect(int imgW, int imgH) const;
    // Map a widget point to a slice-pixel (returns false if outside the image).
    bool widgetToPixel(const QPoint& p, int imgW, int imgH, int* px, int* py) const;

    int axis_;
    ViewState* st_;

    // Drag bookkeeping.
    enum class Drag { None, WindowLevel, Brush, Pan } drag_ = Drag::None;
    QPoint dragStart_;

    // Per-pane zoom (>= 1 = fit) about the cursor, and pan offset in widget pixels.
    double zoom_ = 1.0;
    QPointF panPx_{0.0, 0.0};
    int lastImgW_ = 0, lastImgH_ = 0;  // cached from the last paint for hit-testing
    bool hovering_ = false;
    QPoint hoverPos_;
    // Crosshair-follow is armed only after a plain (no-Shift) move, so it doesn't
    // chase the cursor while Shift stays held after a Shift+drag window/level.
    bool shiftArmed_ = true;
};

}  // namespace lumenwin
