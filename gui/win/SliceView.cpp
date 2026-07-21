#include "SliceView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <algorithm>

namespace lumenwin {

namespace {
constexpr int kTitleH = 26;           // top strip for the axis label
constexpr float kWlSensitivity = 2.0f;  // HU per pixel, matches WindowLevelDrag

const char* axisTitle(int axis) {
    switch (axis) {
        case LUMEN_AXIS_AXIAL: return "Axial";
        case LUMEN_AXIS_CORONAL: return "Coronal";
        case LUMEN_AXIS_SAGITTAL: return "Sagittal";
        default: return "";
    }
}

// In-plane physical pixel size (mm) for the two image axes of `axis`.
void planeSpacing(int axis, float sx, float sy, float sz, float* ax, float* ay) {
    switch (axis) {
        case LUMEN_AXIS_AXIAL:    *ax = sx; *ay = sy; break;  // x-y
        case LUMEN_AXIS_CORONAL:  *ax = sx; *ay = sz; break;  // x-z
        case LUMEN_AXIS_SAGITTAL: *ax = sy; *ay = sz; break;  // y-z
        default:                  *ax = 1;  *ay = 1;  break;
    }
    if (*ax <= 0) *ax = 1;
    if (*ay <= 0) *ay = 1;
}
}  // namespace

SliceView::SliceView(int axis, ViewState* state, QWidget* parent)
    : QWidget(parent), axis_(axis), st_(state) {
    setMouseTracking(false);
    setMinimumSize(160, 160);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
}

QRect SliceView::imageRect(int imgW, int imgH) const {
    LumenVolume* v = st_->volume;
    float sx = 1, sy = 1, sz = 1;
    if (v) lumen_spacing(v, &sx, &sy, &sz);
    float ax = 1, ay = 1;
    planeSpacing(axis_, sx, sy, sz, &ax, &ay);

    const double wmm = imgW * ax;
    const double hmm = imgH * ay;
    const double aspect = (hmm > 0) ? (wmm / hmm) : 1.0;  // width / height

    const int availW = width() - 8;
    const int availH = height() - kTitleH - 8;
    if (availW <= 0 || availH <= 0) return QRect();

    double rw = availW;
    double rh = rw / aspect;
    if (rh > availH) {
        rh = availH;
        rw = rh * aspect;
    }
    const int x = 4 + int((availW - rw) / 2.0);
    const int y = kTitleH + 4 + int((availH - rh) / 2.0);
    return QRect(x, y, int(rw), int(rh));
}

bool SliceView::widgetToPixel(const QPoint& p, int imgW, int imgH, int* px,
                              int* py) const {
    const QRect r = imageRect(imgW, imgH);
    if (!r.isValid() || r.width() <= 0 || r.height() <= 0) return false;
    if (!r.contains(p)) {
        // Still map, but clamp — makes edge painting forgiving.
        if (imgW <= 0 || imgH <= 0) return false;
    }
    double fx = double(p.x() - r.left()) / r.width();
    double fy = double(p.y() - r.top()) / r.height();
    fx = std::clamp(fx, 0.0, 0.999999);
    fy = std::clamp(fy, 0.0, 0.999999);
    *px = std::clamp(int(fx * imgW), 0, imgW - 1);
    *py = std::clamp(int(fy * imgH), 0, imgH - 1);
    return true;
}

void SliceView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 26, 32));

    // Title strip.
    painter.setPen(QColor(210, 214, 224));
    QFont f = painter.font();
    f.setBold(true);
    painter.setFont(f);
    painter.drawText(QRect(8, 4, width() - 16, kTitleH - 4),
                     Qt::AlignVCenter | Qt::AlignLeft, axisTitle(axis_));

    LumenVolume* v = st_->volume;
    if (!v) {
        painter.setPen(QColor(120, 126, 138));
        painter.drawText(rect(), Qt::AlignCenter, "No volume loaded");
        return;
    }

    const int index = st_->sliceIndex[axis_];
    int w = 0, h = 0;
    const unsigned char* px =
        lumen_extract_slice(v, axis_, index, st_->level, st_->window, &w, &h);
    if (!px || w <= 0 || h <= 0) return;

    // Deep-copy: the bridge buffer is only valid until the next extract call.
    QImage slice(reinterpret_cast<const uchar*>(px), w, h, w * 4,
                 QImage::Format_RGBA8888);
    QImage sliceCopy = slice.copy();

    const QRect target = imageRect(w, h);
    lastImgW_ = w;
    lastImgH_ = h;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, sliceCopy);

    // Colored mask overlay (premultiplied RGBA, transparent where unlabelled).
    if (st_->showOverlay) {
        int mw = 0, mh = 0;
        const unsigned char* mask =
            lumen_extract_mask_slice(v, axis_, index, &mw, &mh);
        if (mask && mw == w && mh == h) {
            QImage overlay(reinterpret_cast<const uchar*>(mask), w, h, w * 4,
                           QImage::Format_RGBA8888_Premultiplied);
            painter.drawImage(target, overlay.copy());
        }
    }

    // Crosshair / slice-intersection lines at the shared focus voxel.
    if (st_->showCrosshair) {
        int cx = 0, cy = 0;
        lumen_voxel_to_slice_pixel(v, axis_, st_->focus[0], st_->focus[1],
                                   st_->focus[2], &cx, &cy);
        if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
            const double lx = target.left() + (cx + 0.5) / w * target.width();
            const double ly = target.top() + (cy + 0.5) / h * target.height();
            painter.setPen(QPen(QColor(90, 200, 255, 180), 1));
            painter.drawLine(QPointF(target.left(), ly),
                             QPointF(target.right(), ly));
            painter.drawLine(QPointF(lx, target.top()),
                             QPointF(lx, target.bottom()));
        }
    }

    // Slice counter, bottom-right.
    const int count = lumen_slice_count(v, axis_);
    painter.setPen(QColor(150, 156, 168));
    f.setBold(false);
    painter.setFont(f);
    painter.drawText(QRect(8, height() - 22, width() - 16, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString("%1 / %2").arg(index + 1).arg(count));
}

void SliceView::wheelEvent(QWheelEvent* e) {
    LumenVolume* v = st_->volume;
    if (!v) return;
    const int count = lumen_slice_count(v, axis_);
    if (count <= 0) return;
    const int step = (e->angleDelta().y() > 0) ? -1 : 1;
    const int next = std::clamp(st_->sliceIndex[axis_] + step, 0, count - 1);
    if (next != st_->sliceIndex[axis_]) emit sliceScrolled(axis_, next);
    e->accept();
}

void SliceView::mousePressEvent(QMouseEvent* e) {
    LumenVolume* v = st_->volume;
    if (!v) return;
    const int index = st_->sliceIndex[axis_];
    dragStart_ = e->pos();

    const bool focusChord =
        (e->button() == Qt::MiddleButton) ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier));

    int pxx = 0, pyy = 0;
    const bool inside = widgetToPixel(e->pos(), lastImgW_, lastImgH_, &pxx, &pyy);

    if (focusChord && inside) {
        int x = 0, y = 0, z = 0;
        lumen_slice_pixel_to_voxel(v, axis_, index, pxx, pyy, &x, &y, &z);
        emit focusPicked(x, y, z);
        return;
    }

    // Right-drag is always window/level.
    if (e->button() == Qt::RightButton) {
        drag_ = Drag::WindowLevel;
        return;
    }

    if (e->button() != Qt::LeftButton) return;

    if (st_->segmentInteractive && inside) {
        switch (st_->tool) {
            case Tool::Paint:
            case Tool::Erase:
                emit strokeBegan();
                drag_ = Drag::Brush;
                emit paintStroke(axis_, index, pxx, pyy, st_->brushRadius,
                                 st_->tool == Tool::Paint);
                return;
            case Tool::RegionGrow: {
                emit strokeBegan();
                int x = 0, y = 0, z = 0;
                lumen_slice_pixel_to_voxel(v, axis_, index, pxx, pyy, &x, &y, &z);
                emit floodClicked(x, y, z);
                return;
            }
            case Tool::LevelTrace:
                emit strokeBegan();
                emit levelTraceClicked(axis_, index, pxx, pyy);
                return;
            default:
                break;  // Threshold / None fall through to window/level.
        }
    }

    // Default: left-drag adjusts window/level (the primary W/L control).
    drag_ = Drag::WindowLevel;
}

void SliceView::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ == Drag::None) return;
    LumenVolume* v = st_->volume;
    if (!v) return;

    if (drag_ == Drag::WindowLevel) {
        const QPoint d = e->pos() - dragStart_;
        dragStart_ = e->pos();
        const float dWindow = d.x() * kWlSensitivity;
        const float dLevel = -d.y() * kWlSensitivity;
        emit windowLevelDragged(dLevel, dWindow);
        return;
    }

    if (drag_ == Drag::Brush) {
        int pxx = 0, pyy = 0;
        if (widgetToPixel(e->pos(), lastImgW_, lastImgH_, &pxx, &pyy)) {
            emit paintStroke(axis_, st_->sliceIndex[axis_], pxx, pyy,
                             st_->brushRadius, st_->tool == Tool::Paint);
        }
    }
}

void SliceView::mouseReleaseEvent(QMouseEvent*) { drag_ = Drag::None; }

}  // namespace lumenwin
