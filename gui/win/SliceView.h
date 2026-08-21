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

public slots:
    void zoomStep(double factor);  // zoom around the pane centre
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
    void strokeEnded();

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    // Geometry of the drawn slice image inside the widget (aspect-preserving).
    QRect imageRect(int imgW, int imgH) const;
    // Map a widget point to a slice-pixel (returns false if outside the image).
    bool widgetToPixel(const QPoint& p, int imgW, int imgH, int* px, int* py) const;
    void buildZoomBar();
    void positionZoomBar();
    // Clamp sliceIndex[axis_] + delta into range and emit sliceScrolled if it
    // actually moved. Shared by wheelEvent and keyPressEvent (Up/Down).
    void stepSlice(int delta);

    int axis_;
    ViewState* st_;
    QWidget* zoomBar_ = nullptr;

    // Drag bookkeeping.
    enum class Drag { None, WindowLevel, Brush } drag_ = Drag::None;
    QPoint dragStart_;
    int lastImgW_ = 0, lastImgH_ = 0;  // cached from the last paint for hit-testing
    bool hovering_ = false;
    QPoint hoverPos_;

    // Navigation state mirrors the Mac canvas: right-drag zooms around the
    // cursor anchor, middle-drag pans, and wheel deltas accumulate until they
    // represent one slice step on either a mouse or high-resolution wheel.
    enum class Navigation { None, Zoom, Pan } navigation_ = Navigation::None;
    QPoint zoomAnchor_;
    QPoint lastNavigationPos_;
    double zoom_ = 1.0;
    QPointF pan_;
    double wheelAccum_ = 0.0;

    // QImage owns a private copy so another pane can reuse the bridge scratch
    // buffers without invalidating this pane's next repaint. The keys avoid
    // repeating the expensive RGBA decode/copy while only UI chrome changes.
    LumenVolume* cachedVolume_ = nullptr;
    bool sliceCacheValid_ = false;
    int cachedSliceIndex_ = 0;
    float cachedLevel_ = 0.0f;
    float cachedWindow_ = 0.0f;
    QImage cachedSlice_;
    bool overlayCacheValid_ = false;
    int cachedOverlayIndex_ = 0;
    unsigned long long cachedOverlayRevision_ = 0;
    float cachedOverlayThresholdLo_ = 0.0f;
    float cachedOverlayThresholdHi_ = 0.0f;
    QImage cachedOverlay_;
};

}  // namespace lumenwin
