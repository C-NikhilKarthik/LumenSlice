// Client-side fiducial markups (Point / Line / Plane) dropped on the slice panes
// and shown in the 3D view. Mirrors the macOS MarkupModel: points are stored in
// VOXEL coordinates (the single source of truth); the 3D view converts to mm
// (voxel x spacing, same as the mesh) and the slices project back to pixels. The
// core has no markup concept, so this lives entirely in the Windows UI layer —
// no bridge code.
#pragma once

#include <QColor>
#include <QString>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lumenwin {

class MarkupModel {
public:
    enum class Kind { Point = 0, Line = 1, Plane = 2 };
    static int pointsNeeded(Kind k) {
        return k == Kind::Point ? 1 : (k == Kind::Line ? 2 : 3);
    }
    static const char* title(Kind k) {
        return k == Kind::Point ? "Point" : (k == Kind::Line ? "Line" : "Plane");
    }

    struct Markup {
        int id = 0;
        Kind kind = Kind::Point;
        std::vector<std::array<int, 3>> voxels;
        int colorIndex = 0;
        QString name;
        bool visible = true;
    };

    // Eight distinct marker colours, independent of the segment palette.
    static const std::vector<QColor>& palette() {
        static const std::vector<QColor> p = {
            QColor(255, 220, 60),  QColor(80, 220, 240), QColor(90, 220, 110),
            QColor(255, 150, 70),  QColor(255, 120, 190), QColor(180, 120, 255),
            QColor(120, 230, 200), QColor(240, 80, 80)};
        return p;
    }
    static QColor paletteColor(int index) { return palette()[wrap(index)]; }

    // A fresh volume invalidates all markups and restarts colour/numbering.
    void resetForVolume(float sx, float sy, float sz) {
        markups_.clear();
        pending_.clear();
        placing_ = false;
        nextColorIndex_ = 0;
        counter_ = 1;
        autoAdvance_ = true;
        sx_ = sx;
        sy_ = sy;
        sz_ = sz;
    }

    // --- Placement ---
    bool placing() const { return placing_; }
    void setPlacing(bool on) { placing_ = on; }
    Kind kind() const { return kind_; }
    void setKind(Kind k) { kind_ = k; }
    int nextColorIndex() const { return nextColorIndex_; }
    void pickNextColor(int index) {
        nextColorIndex_ = wrap(index);
        autoAdvance_ = false;
    }
    const std::vector<std::array<int, 3>>& pending() const { return pending_; }

    // Add a defining point; returns true when a markup was completed.
    bool place(int x, int y, int z) {
        if (!placing_) return false;
        pending_.push_back({x, y, z});
        if (int(pending_.size()) >= pointsNeeded(kind_)) {
            Markup m;
            m.id = idCounter_++;
            m.kind = kind_;
            m.voxels = pending_;
            m.colorIndex = nextColorIndex_;
            m.name = QString("%1 %2").arg(title(kind_)).arg(counter_++);
            markups_.push_back(std::move(m));
            if (autoAdvance_)
                nextColorIndex_ = (nextColorIndex_ + 1) % int(palette().size());
            pending_.clear();
            return true;
        }
        return false;
    }
    void cancelPending() { pending_.clear(); }

    // --- List ---
    const std::vector<Markup>& markups() const { return markups_; }
    void remove(int id) {
        markups_.erase(std::remove_if(markups_.begin(), markups_.end(),
                                      [id](const Markup& m) { return m.id == id; }),
                       markups_.end());
    }
    void removeAll() {
        markups_.clear();
        pending_.clear();
    }
    void rename(int id, const QString& name) {
        if (Markup* m = find(id))
            m->name = name.isEmpty() ? QString(title(m->kind)) : name;
    }
    void setColorIndex(int id, int index) {
        if (Markup* m = find(id)) m->colorIndex = wrap(index);
    }
    void setVisible(int id, bool visible) {
        if (Markup* m = find(id)) m->visible = visible;
    }

    // --- Helpers ---
    QColor color(const Markup& m) const { return paletteColor(m.colorIndex); }
    QColor pendingColor() const { return paletteColor(nextColorIndex_); }
    QVector3D mm(const std::array<int, 3>& v) const {
        return QVector3D(v[0] * sx_, v[1] * sy_, v[2] * sz_);
    }
    QString measurementText(const Markup& m) const {
        if (m.voxels.size() < 2) return {};
        if (m.kind == Kind::Line) {
            const double d = (mm(m.voxels[1]) - mm(m.voxels[0])).length();
            return QString("Length %1 mm").arg(formatMeasurement(d));
        }
        if (m.kind == Kind::Plane && m.voxels.size() >= 3) {
            const QVector3D a = mm(m.voxels[1]) - mm(m.voxels[0]);
            const QVector3D b = mm(m.voxels[2]) - mm(m.voxels[0]);
            const double area = 0.5 * QVector3D::crossProduct(a, b).length();
            return QString("Area %1 mm²").arg(formatMeasurement(area));
        }
        return {};
    }
    QString liveLineMeasurementText(const std::array<int, 3>& from,
                                    const std::array<int, 3>& to) const {
        const double d = (mm(to) - mm(from)).length();
        return QString("Length %1 mm").arg(formatMeasurement(d));
    }
    // Whether a voxel lies on the current slice of `axis`.
    static bool onSlice(const std::array<int, 3>& v, int axis, int sliceIndex) {
        const int idx = axis == 0 ? v[2] : (axis == 1 ? v[1] : v[0]);
        return idx == sliceIndex;
    }

private:
    static QString formatMeasurement(double value) {
        if (value >= 100.0) return QString::number(value, 'f', 0);
        if (value >= 10.0) return QString::number(value, 'f', 1);
        return QString::number(value, 'f', 2);
    }

    static int wrap(int i) {
        const int n = int(palette().size());
        return ((i % n) + n) % n;
    }
    Markup* find(int id) {
        for (Markup& m : markups_)
            if (m.id == id) return &m;
        return nullptr;
    }

    bool placing_ = false;
    Kind kind_ = Kind::Point;
    int nextColorIndex_ = 0;
    bool autoAdvance_ = true;
    int counter_ = 1;
    int idCounter_ = 1;
    std::vector<std::array<int, 3>> pending_;
    std::vector<Markup> markups_;
    float sx_ = 1, sy_ = 1, sz_ = 1;
};

}  // namespace lumenwin
