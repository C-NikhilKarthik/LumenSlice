#include "SliceView.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

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
    buildZoomBar();
}

void SliceView::buildZoomBar() {
    zoomBar_ = new QWidget(this);
    zoomBar_->setStyleSheet("background:rgba(18,20,26,180);border-radius:6px;");
    auto* h = new QHBoxLayout(zoomBar_);
    h->setContentsMargins(4, 2, 4, 2);
    h->setSpacing(1);
    auto mk = [&](QStyle::StandardPixmap icon, const QString& tip) {
        auto* b = new QToolButton(zoomBar_);
        b->setIcon(style()->standardIcon(icon));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setStyleSheet(
            "QToolButton{color:#e6e8ee;border:none;padding:2px 7px;font-size:16px;}"
            "QToolButton:hover{background:rgba(255,255,255,32);border-radius:4px;}");
        h->addWidget(b);
        return b;
    };
    auto* info = mk(QStyle::SP_MessageBoxInformation,
                    "How to zoom, pan, and navigate this view");
    connect(info, &QToolButton::clicked, this, [this] {
        QMessageBox::information(
            this, "Slice navigation",
            "Ctrl + wheel: Zoom\n"
            "Right-drag: Zoom\n"
            "Middle-drag or Alt + left-drag: Pan (when zoomed in)\n"
            "Wheel or Up/Down: Previous / next slice\n"
            "Ctrl + click: Recenter all three views");
    });
    connect(mk(QStyle::SP_ArrowUp, "Zoom in"), &QToolButton::clicked, this,
            [this] { zoomStep(1.25); });
    connect(mk(QStyle::SP_ArrowDown, "Zoom out"), &QToolButton::clicked, this,
            [this] { zoomStep(0.8); });
    connect(mk(QStyle::SP_BrowserReload, "Reset zoom"), &QToolButton::clicked, this,
            &SliceView::resetZoom);
    connect(mk(QStyle::SP_TitleBarMaxButton, "Maximize / restore this view"), &QToolButton::clicked, this,
            [this] { emit doubleClicked(); });
    zoomBar_->adjustSize();
}

void SliceView::positionZoomBar() {
    if (!zoomBar_) return;
    zoomBar_->adjustSize();
    zoomBar_->move(width() - zoomBar_->width() - 8, 6);
    zoomBar_->raise();
}

void SliceView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    positionZoomBar();
}

void SliceView::zoomStep(double factor) {
    zoomAnchor_ = QPoint(width() / 2, height() / 2);
    zoom_ = std::clamp(zoom_ * factor, 1.0, 8.0);
    if (zoom_ <= 1.0) pan_ = QPointF(0, 0);
    update();
}

void SliceView::resetZoom() {
    zoom_ = 1.0;
    pan_ = QPointF(0, 0);
    update();
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
    QRect fitted(x, y, int(rw), int(rh));
    if (zoom_ <= 1.0 && pan_ == QPointF(0, 0)) return fitted;

    const QPointF anchor = zoomAnchor_.isNull() ? fitted.center() : zoomAnchor_;
    const double nw = fitted.width() * zoom_;
    const double nh = fitted.height() * zoom_;
    const double nx = anchor.x() + (fitted.left() - anchor.x()) * zoom_ + pan_.x();
    const double ny = anchor.y() + (fitted.top() - anchor.y()) * zoom_ + pan_.y();
    return QRect(qRound(nx), qRound(ny), qRound(nw), qRound(nh));
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
        cachedVolume_ = nullptr;
        sliceCacheValid_ = false;
        overlayCacheValid_ = false;
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
    if (!sliceCacheValid_ || cachedVolume_ != v || cachedSliceIndex_ != index ||
        cachedLevel_ != st_->level || cachedWindow_ != st_->window) {
        const unsigned char* px = lumen_extract_slice(
            v, axis_, index, st_->level, st_->window, &w, &h);
        if (!px || w <= 0 || h <= 0) return;
        QImage slice(reinterpret_cast<const uchar*>(px), w, h, w * 4,
                     QImage::Format_RGBA8888);
        cachedSlice_ = slice.copy();
        cachedVolume_ = v;
        cachedSliceIndex_ = index;
        cachedLevel_ = st_->level;
        cachedWindow_ = st_->window;
        sliceCacheValid_ = true;
        overlayCacheValid_ = false;
    } else {
        w = cachedSlice_.width();
        h = cachedSlice_.height();
    }

    const QRect target = imageRect(w, h);
    lastImgW_ = w;
    lastImgH_ = h;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(target, cachedSlice_);

    // Colored mask overlay (premultiplied RGBA, transparent where unlabelled).
    if (st_->showOverlay && !st_->busy) {
        const unsigned long long revision = lumen_seg_revision(v);
        // Show the live threshold preview only once the user has engaged threshold;
        // an untouched load falls through to the (empty) mask overlay instead.
        const bool showThresholdPreview =
            st_->tool == Tool::Threshold && st_->thresholdPreviewArmed;
        const int activeSegment = lumen_seg_active(v);
        if (!overlayCacheValid_ || cachedVolume_ != v ||
            cachedOverlayIndex_ != index || cachedOverlayRevision_ != revision ||
            cachedOverlayWasThreshold_ != showThresholdPreview ||
            (showThresholdPreview &&
             cachedOverlayActiveSegment_ != activeSegment) ||
            (showThresholdPreview &&
             (cachedOverlayThresholdLo_ != st_->thresholdLo ||
              cachedOverlayThresholdHi_ != st_->thresholdHi))) {
            int mw = 0, mh = 0;
            const unsigned char* mask = nullptr;
            if (showThresholdPreview) {
                mask = lumen_extract_threshold_slice(v, axis_, index,
                                                     st_->thresholdLo,
                                                     st_->thresholdHi,
                                                     &mw, &mh);
            } else {
                mask = lumen_extract_mask_slice(v, axis_, index, &mw, &mh);
            }
            cachedOverlay_ = (mask && mw == w && mh == h)
                                 ? QImage(reinterpret_cast<const uchar*>(mask), w,
                                          h, w * 4,
                                          QImage::Format_RGBA8888_Premultiplied)
                                       .copy()
                                 : QImage();
            cachedOverlayIndex_ = index;
            cachedOverlayRevision_ = revision;
            cachedOverlayThresholdLo_ = st_->thresholdLo;
            cachedOverlayThresholdHi_ = st_->thresholdHi;
            cachedOverlayWasThreshold_ = showThresholdPreview;
            cachedOverlayActiveSegment_ = activeSegment;
            overlayCacheValid_ = true;
        }
        if (!cachedOverlay_.isNull()) painter.drawImage(target, cachedOverlay_);
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

    // Slicer-style markup representation: draw completed line/plane geometry when
    // its defining points lie on this slice, with measurements in physical units.
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
            if (m.voxels.size() >= 2) {
                const float slice = float(index);
                int volumeD = 0;
                lumen_dims(v, nullptr, nullptr, &volumeD);
                auto coord = [this](const QVector3D& p) {
                    return axis_ == LUMEN_AXIS_AXIAL
                               ? p.z()
                               : (axis_ == LUMEN_AXIS_CORONAL ? p.y() : p.x());
                };
                auto projectVoxel = [&](const QVector3D& p) {
                    const float px = axis_ == LUMEN_AXIS_AXIAL ? p.x() :
                                     (axis_ == LUMEN_AXIS_CORONAL ? p.x() : p.y());
                    const float py = axis_ == LUMEN_AXIS_AXIAL
                                         ? p.y()
                                         : float(volumeD - 1) - p.z();
                    return QPointF(target.left() + (px + 0.5f) / w * target.width(),
                                   target.top() + (py + 0.5f) / h * target.height());
                };
                auto addIntersection = [](std::vector<QVector3D>& out,
                                           const QVector3D& p) {
                    for (const auto& existing : out)
                        if ((existing - p).lengthSquared() < 0.0001f) return;
                    out.push_back(p);
                };
                std::vector<QVector3D> intersections;
                auto edge = [&](const QVector3D& a, const QVector3D& b) {
                    const float da = coord(a) - slice, db = coord(b) - slice;
                    if (std::abs(da) < 0.001f) { addIntersection(intersections, a); return; }
                    if (std::abs(db) < 0.001f) { addIntersection(intersections, b); return; }
                    if (da * db < 0.0f) {
                        addIntersection(intersections, a + (b - a) * (da / (da - db)));
                    }
                };
                const auto point = [](const std::array<int, 3>& p) {
                    return QVector3D(float(p[0]), float(p[1]), float(p[2]));
                };
                if (m.kind == MarkupModel::Kind::Line) {
                    edge(point(m.voxels[0]), point(m.voxels[1]));
                } else if (m.kind == MarkupModel::Kind::Plane && m.voxels.size() >= 3) {
                    edge(point(m.voxels[0]), point(m.voxels[1]));
                    edge(point(m.voxels[1]), point(m.voxels[2]));
                    edge(point(m.voxels[2]), point(m.voxels[0]));
                }
                std::vector<QPointF> pts;
                for (const auto& hit : intersections) pts.push_back(projectVoxel(hit));
                painter.setPen(QPen(col, 2));
                if (m.kind == MarkupModel::Kind::Line && pts.size() == 2) {
                    painter.drawLine(pts[0], pts[1]);
                } else if (m.kind == MarkupModel::Kind::Plane && pts.size() == 3) {
                    QPolygonF polygon;
                    for (const auto& pt : pts) polygon << pt;
                    painter.setBrush(QColor(col.red(), col.green(), col.blue(), 45));
                    painter.drawPolygon(polygon);
                } else if (pts.size() == 2) {
                    painter.drawLine(pts[0], pts[1]);
                }
                painter.setBrush(col);
                for (const auto& pt : pts) painter.drawEllipse(pt, 3.5, 3.5);
                const QString measurement = st_->markups->measurementText(m);
                if (!measurement.isEmpty() && !pts.empty()) {
                    const QPointF anchor = pts.front();
                    painter.setPen(Qt::white);
                    painter.setBrush(QColor(0, 0, 0, 170));
                    painter.drawRoundedRect(QRectF(anchor.x() + 8, anchor.y() - 18,
                                                   measurement.size() * 7 + 12, 18), 5, 5);
                    painter.drawText(QRectF(anchor.x() + 14, anchor.y() - 17,
                                            measurement.size() * 7, 16),
                                     Qt::AlignLeft | Qt::AlignVCenter, measurement);
                }
            }
        }
        // Slicer-style rubber-band geometry from the last pending point to the
        // current pointer. Project it into every orthogonal pane so the preview
        // remains visible while the next point is being positioned.
        if (st_->markupPlacing && !st_->markups->pending().empty() && hovering_) {
            int px = 0, py = 0;
            if (widgetToPixel(hoverPos_, w, h, &px, &py)) {
                int x = 0, y = 0, z = 0;
                lumen_slice_pixel_to_voxel(v, axis_, index, px, py, &x, &y, &z);
                std::vector<std::array<int, 3>> previewVoxels;
                const auto& pending = st_->markups->pending();
                const auto kind = st_->markups->kind();
                if (kind == MarkupModel::Kind::Plane && pending.size() >= 2) {
                    previewVoxels.push_back(pending[0]);
                    previewVoxels.push_back(pending[1]);
                } else {
                    previewVoxels.push_back(pending.back());
                }
                previewVoxels.push_back({x, y, z});
                std::vector<QPointF> preview;
                auto addPreviewPoint = [&](const std::array<int, 3>& vp) {
                    int qx = 0, qy = 0;
                    lumen_voxel_to_slice_pixel(v, axis_, vp[0], vp[1], vp[2], &qx, &qy);
                    preview.emplace_back(target.left() + (qx + 0.5) / w * target.width(),
                                         target.top() + (qy + 0.5) / h * target.height());
                };
                for (const auto& vp : previewVoxels) addPreviewPoint(vp);
                const QColor previewColor = st_->markups->pendingColor();
                painter.setPen(QPen(previewColor, 1.5, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                if (preview.size() >= 2 && (kind == MarkupModel::Kind::Line ||
                                             preview.size() < 3)) {
                    painter.drawLine(preview[preview.size() - 2], preview.back());
                } else if (kind == MarkupModel::Kind::Plane && preview.size() == 3) {
                    QPolygonF polygon;
                    for (const auto& pt : preview) polygon << pt;
                    painter.setBrush(QColor(previewColor.red(), previewColor.green(),
                                            previewColor.blue(), 18));
                    painter.drawPolygon(polygon);
                }
                if (preview.size() >= 2) {
                    const QString measurement = st_->markups->liveLineMeasurementText(
                        previewVoxels[previewVoxels.size() - 2], previewVoxels.back());
                    const QPointF a = preview[preview.size() - 2];
                    const QPointF b = preview.back();
                    const QPointF center = (a + b) * 0.5;
                    painter.setPen(Qt::white);
                    painter.setBrush(QColor(0, 0, 0, 150));
                    painter.drawRoundedRect(QRectF(center.x() - 42, center.y() - 20,
                                                   measurement.size() * 7 + 12, 18), 5, 5);
                    painter.drawText(QRectF(center.x() - 36, center.y() - 19,
                                            measurement.size() * 7, 16),
                                     Qt::AlignLeft | Qt::AlignVCenter, measurement);
                }
            }
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
    if (e->modifiers() & Qt::ControlModifier) {
        const QPoint delta = e->angleDelta().isNull() ? e->pixelDelta() : e->angleDelta();
        const double amount = delta.y() != 0 ? double(delta.y()) / 120.0 : double(delta.x()) / 120.0;
        if (amount != 0.0) {
            zoomAnchor_ = e->position().toPoint();
            zoom_ = std::clamp(zoom_ * std::pow(1.15, amount), 1.0, 8.0);
            if (zoom_ <= 1.0) pan_ = QPointF(0, 0);
            update();
        }
        e->accept();
        return;
    }
    const int count = lumen_slice_count(v, axis_);
    if (count <= 0) return;
    // Normalize high-resolution and regular wheels like the macOS catcher.
    const QPoint pixel = e->pixelDelta();
    const QPoint angle = e->angleDelta();
    const double delta = pixel.y() != 0
                             ? double(pixel.y())
                             : double(angle.y()) / 12.0;
    if (delta == 0.0) {
        e->accept();
        return;
    }
    wheelAccum_ += delta;
    while (std::abs(wheelAccum_) >= 10.0) {
        const int step = wheelAccum_ > 0 ? 1 : -1;
        wheelAccum_ -= step * 10.0;
        stepSlice(step);
    }
    e->accept();
}

// Step this pane's slice, clamped to [0, count-1]. Shared by the wheel and the
// Up/Down arrow keys so all three stay in sync with the slider and step buttons.
void SliceView::stepSlice(int delta) {
    LumenVolume* v = st_->volume;
    if (!v) return;
    const int count = lumen_slice_count(v, axis_);
    if (count <= 0) return;
    const int current = std::clamp(st_->sliceIndex[axis_], 0, count - 1);
    const int next = std::clamp(current + delta, 0, count - 1);
    if (next != st_->sliceIndex[axis_]) emit sliceScrolled(axis_, next);
}

// Up = previous slice (-1), Down = next (+1) - matching the wheel and the on-screen
// step buttons. The pane grabs focus on hover (see enterEvent) so the arrows drive
// whichever pane the cursor is over.
void SliceView::keyPressEvent(QKeyEvent* e) {
    switch (e->key()) {
        case Qt::Key_Up:   stepSlice(-1); e->accept(); return;
        case Qt::Key_Down: stepSlice(1);  e->accept(); return;
        default: QWidget::keyPressEvent(e);
    }
}

// Give the hovered pane keyboard focus so the arrow keys act on it - but never
// steal focus from a spin box or text field the user is editing.
void SliceView::enterEvent(QEnterEvent* e) {
    QWidget* focused = QApplication::focusWidget();
    const bool editing = qobject_cast<QAbstractSpinBox*>(focused) != nullptr ||
                         qobject_cast<QLineEdit*>(focused) != nullptr;
    if (!editing) setFocus(Qt::MouseFocusReason);
    QWidget::enterEvent(e);
}

void SliceView::mousePressEvent(QMouseEvent* e) {
    LumenVolume* v = st_->volume;
    if (!v) return;
    const int index = st_->sliceIndex[axis_];
    dragStart_ = e->pos();

    const bool focusChord =
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier));

    int pxx = 0, pyy = 0;
    const bool inside = widgetToPixel(e->pos(), lastImgW_, lastImgH_, &pxx, &pyy);

    if (focusChord && inside) {
        int x = 0, y = 0, z = 0;
        lumen_slice_pixel_to_voxel(v, axis_, index, pxx, pyy, &x, &y, &z);
        emit focusPicked(x, y, z);
        return;
    }

    const bool altPan = e->button() == Qt::LeftButton &&
                        (e->modifiers() & Qt::AltModifier);
    if (e->button() == Qt::RightButton || e->button() == Qt::MiddleButton ||
        altPan) {
        if (!inside) return;
        navigation_ = e->button() == Qt::RightButton ? Navigation::Zoom
                                                      : Navigation::Pan;
        lastNavigationPos_ = e->pos();
        if (navigation_ == Navigation::Zoom) zoomAnchor_ = e->pos();
        setCursor(navigation_ == Navigation::Zoom ? Qt::SizeVerCursor
                                                   : Qt::SizeAllCursor);
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
    if (drag_ == Drag::None && navigation_ == Navigation::None &&
        (e->modifiers() & Qt::ShiftModifier) && st_->volume) {
        int px = 0, py = 0;
        if (widgetToPixel(e->pos(), lastImgW_, lastImgH_, &px, &py)) {
            int x = 0, y = 0, z = 0;
            lumen_slice_pixel_to_voxel(st_->volume, axis_, st_->sliceIndex[axis_],
                                       px, py, &x, &y, &z);
            emit focusPicked(x, y, z);
        }
    }
    if (navigation_ == Navigation::Zoom) {
        const int dy = e->pos().y() - lastNavigationPos_.y();
        zoom_ = std::clamp(zoom_ * std::exp(-double(dy) * 0.01), 1.0, 8.0);
        if (zoom_ <= 1.0) pan_ = QPointF(0, 0);
        lastNavigationPos_ = e->pos();
        update();
        return;
    }
    if (navigation_ == Navigation::Pan) {
        if (zoom_ > 1.0) {
            const QPoint d = e->pos() - lastNavigationPos_;
            pan_ += QPointF(d.x(), d.y());
            const QRect fitted = imageRect(lastImgW_, lastImgH_);
            const double maxX = fitted.width() * (zoom_ - 1.0) / (2.0 * zoom_);
            const double maxY = fitted.height() * (zoom_ - 1.0) / (2.0 * zoom_);
            pan_.setX(std::clamp(pan_.x(), -maxX, maxX));
            pan_.setY(std::clamp(pan_.y(), -maxY, maxY));
        }
        lastNavigationPos_ = e->pos();
        update();
        return;
    }
    if (drag_ == Drag::None) {
        if (st_->markupPlacing ||
            (st_->segmentInteractive &&
            (st_->tool == Tool::Paint || st_->tool == Tool::Erase))
        )
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

void SliceView::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ == Drag::Brush) emit strokeEnded();
    drag_ = Drag::None;
    navigation_ = Navigation::None;
    unsetCursor();
}

void SliceView::mouseDoubleClickEvent(QMouseEvent*) { emit doubleClicked(); }

void SliceView::leaveEvent(QEvent*) {
    hovering_ = false;
    update();
}

}  // namespace lumenwin
