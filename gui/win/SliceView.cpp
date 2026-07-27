#include "SliceView.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QWheelEvent>
#include <algorithm>

#include "MarkupModel.h"

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
    setMouseTracking(true);  // for the brush-ring cursor preview
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
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Rounded card: fill the gaps with the canvas background, then the card, and
    // clip everything else to it so the pane reads as a distinct tile in the quad.
    painter.fillRect(rect(), QColor(0x1b, 0x1d, 0x23));
    const QRectF card = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, 12, 12);
    painter.fillPath(cardPath, QColor(0x12, 0x14, 0x18));
    painter.setClipPath(cardPath);

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
        painter.setClipping(false);
        painter.setPen(QPen(QColor(0x2f, 0x34, 0x40), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(cardPath);
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

    // Crosshair / slice-intersection lines at the shared focus voxel. Each plane
    // has its own colour: axial red, coronal green, sagittal yellow.
    if (st_->showCrosshair) {
        int cx = 0, cy = 0;
        lumen_voxel_to_slice_pixel(v, axis_, st_->focus[0], st_->focus[1],
                                   st_->focus[2], &cx, &cy);
        if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
            const double lx = target.left() + (cx + 0.5) / w * target.width();
            const double ly = target.top() + (cy + 0.5) / h * target.height();
            QColor ch = axis_ == LUMEN_AXIS_AXIAL
                            ? QColor(235, 80, 80)
                            : (axis_ == LUMEN_AXIS_CORONAL ? QColor(90, 210, 100)
                                                           : QColor(240, 210, 60));
            ch.setAlpha(190);
            painter.setPen(QPen(ch, 1));
            painter.drawLine(QPointF(target.left(), ly),
                             QPointF(target.right(), ly));
            painter.drawLine(QPointF(lx, target.top()),
                             QPointF(lx, target.bottom()));
        }
    }

    // Anatomical orientation labels at the four edges (approximate radiological
    // convention per plane).
    if (st_->showOrientationLabels) {
        const char* lbl[4];  // left, right, top, bottom
        if (axis_ == LUMEN_AXIS_AXIAL) {
            lbl[0] = "R"; lbl[1] = "L"; lbl[2] = "A"; lbl[3] = "P";
        } else if (axis_ == LUMEN_AXIS_CORONAL) {
            lbl[0] = "R"; lbl[1] = "L"; lbl[2] = "S"; lbl[3] = "I";
        } else {
            lbl[0] = "A"; lbl[1] = "P"; lbl[2] = "S"; lbl[3] = "I";
        }
        painter.setPen(QColor(150, 200, 230, 200));
        const int cyv = target.center().y();
        const int cxv = target.center().x();
        painter.drawText(target.left() + 3, cyv + 5, lbl[0]);
        painter.drawText(target.right() - 12, cyv + 5, lbl[1]);
        painter.drawText(cxv - 4, target.top() + 14, lbl[2]);
        painter.drawText(cxv - 4, target.bottom() - 5, lbl[3]);
    }

    // Brush-ring cursor preview while a paint/erase tool is active.
    if (st_->segmentInteractive && hovering_ &&
        (st_->tool == Tool::Paint || st_->tool == Tool::Erase)) {
        const double scale = double(target.width()) / w;  // px -> display
        const double rr = std::max(2.0, st_->brushRadius * scale);
        painter.setPen(QPen(st_->tool == Tool::Erase ? QColor(255, 120, 120)
                                                     : QColor(120, 220, 255),
                            1, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(hoverPos_), rr, rr);
    }

    // Markup dots for defining points that lie on this slice (+ pending points).
    if (st_->markups) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        auto drawDot = [&](const std::array<int, 3>& vp, const QColor& col,
                           bool filled) {
            int px = 0, py = 0;
            lumen_voxel_to_slice_pixel(v, axis_, vp[0], vp[1], vp[2], &px, &py);
            if (px < 0 || px >= w || py < 0 || py >= h) return;
            const double dx = target.left() + (px + 0.5) / w * target.width();
            const double dy = target.top() + (py + 0.5) / h * target.height();
            painter.setPen(QPen(col, 2));
            painter.setBrush(filled ? QBrush(col) : Qt::NoBrush);
            painter.drawEllipse(QPointF(dx, dy), 4.5, 4.5);
        };
        for (const auto& m : st_->markups->markups()) {
            if (!m.visible) continue;
            const QColor col = st_->markups->color(m);
            for (const auto& vp : m.voxels)
                if (MarkupModel::onSlice(vp, axis_, index)) drawDot(vp, col, true);
        }
        const QColor pc = st_->markups->pendingColor();
        for (const auto& vp : st_->markups->pending())
            if (MarkupModel::onSlice(vp, axis_, index)) drawDot(vp, pc, false);
    }

    // Slice counter, bottom-right.
    const int count = lumen_slice_count(v, axis_);
    painter.setPen(QColor(150, 156, 168));
    f.setBold(false);
    painter.setFont(f);
    painter.drawText(QRect(8, height() - 22, width() - 16, 18),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString("%1 / %2").arg(index + 1).arg(count));

    // Card border on top of everything.
    painter.setClipping(false);
    painter.setPen(QPen(QColor(0x2f, 0x34, 0x40), 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(cardPath);
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

    // Markup placement: a plain left-click drops a defining point.
    if (st_->markupPlacing && e->button() == Qt::LeftButton && inside) {
        int x = 0, y = 0, z = 0;
        lumen_slice_pixel_to_voxel(v, axis_, index, pxx, pyy, &x, &y, &z);
        emit markupPointPicked(x, y, z);
        return;
    }

    // Window/level is on a modifier drag: right-button, or Shift+left.
    if (e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ShiftModifier))) {
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

    // Default: a plain left-click locates — set the shared focus and link panes.
    if (inside) {
        int x = 0, y = 0, z = 0;
        lumen_slice_pixel_to_voxel(v, axis_, index, pxx, pyy, &x, &y, &z);
        emit focusPicked(x, y, z);
    }
}

void SliceView::mouseMoveEvent(QMouseEvent* e) {
    // Track hover for the brush-ring cursor preview.
    hoverPos_ = e->pos();
    hovering_ = true;
    if (drag_ == Drag::None) {
        if (st_->segmentInteractive &&
            (st_->tool == Tool::Paint || st_->tool == Tool::Erase))
            update();  // redraw the ring at the new position
        return;
    }
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

void SliceView::mouseDoubleClickEvent(QMouseEvent*) { emit doubleClicked(); }

void SliceView::leaveEvent(QEvent*) {
    hovering_ = false;
    update();
}

}  // namespace lumenwin
