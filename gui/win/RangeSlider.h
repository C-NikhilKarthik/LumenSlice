// A horizontal two-thumb range slider (low / high), for the live threshold
// window. Mirrors app/Shell/RangeSlider.swift: drag either handle to set the
// bounds; the values stay ordered (low <= high). Header-only (AUTOMOC picks up
// the Q_OBJECT when it is included).
#pragma once

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>

namespace lumenwin {

class RangeSlider : public QWidget {
    Q_OBJECT
public:
    explicit RangeSlider(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(28);
        setMinimumWidth(80);
    }

    void setBounds(double lo, double hi) {
        min_ = lo;
        max_ = std::max(hi, lo + 1);
        low_ = std::clamp(low_, min_, max_);
        high_ = std::clamp(high_, min_, max_);
        update();
    }
    void setValues(double lo, double hi) {
        low_ = std::clamp(lo, min_, max_);
        high_ = std::clamp(std::max(hi, low_), min_, max_);
        update();
    }
    double lowValue() const { return low_; }
    double highValue() const { return high_; }

signals:
    void editingStarted();
    void rangeChanged(double low, double high);

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const double y = height() / 2.0;
        const double xl = pos(low_), xh = pos(high_);
        p.setPen(QPen(QColor(90, 94, 104), 3));
        p.drawLine(QPointF(margin_, y), QPointF(width() - margin_, y));
        p.setPen(QPen(QColor(90, 150, 240), 3));
        p.drawLine(QPointF(xl, y), QPointF(xh, y));
        p.setPen(QPen(QColor(30, 32, 38), 1));
        p.setBrush(QColor(220, 224, 232));
        p.drawEllipse(QPointF(xl, y), 7, 7);
        p.drawEllipse(QPointF(xh, y), 7, 7);
    }

    void mousePressEvent(QMouseEvent* e) override {
        const double x = e->position().x();
        dragLow_ = std::abs(x - pos(low_)) <= std::abs(x - pos(high_));
        emit editingStarted();
        setFromX(x);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (e->buttons() & Qt::LeftButton) setFromX(e->position().x());
    }

private:
    double pos(double v) const {
        const double t = (v - min_) / (max_ - min_);
        return margin_ + t * (width() - 2 * margin_);
    }
    void setFromX(double x) {
        const double t =
            std::clamp((x - margin_) / (width() - 2 * margin_), 0.0, 1.0);
        const double v = min_ + t * (max_ - min_);
        if (dragLow_)
            low_ = std::min(v, high_);
        else
            high_ = std::max(v, low_);
        update();
        emit rangeChanged(low_, high_);
    }

    double min_ = 0, max_ = 1;
    double low_ = 0, high_ = 1;
    bool dragLow_ = true;
    const double margin_ = 9.0;
};

}  // namespace lumenwin
