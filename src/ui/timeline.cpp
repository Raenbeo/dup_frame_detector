#include "ui/timeline.hpp"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace dfd {

Timeline::Timeline(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(40);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void Timeline::set_frame_count(int n) {
    count_ = std::max(0, n);
    current_ = 0;
    start_ = loop_start_ = loop_end_ = -1;
    update();
}

void Timeline::set_current(int f) {
    current_ = f;
    update();
}

void Timeline::set_start(int f) {
    start_ = f;
    update();
}

void Timeline::set_loop(int s, int e) {
    loop_start_ = s;
    loop_end_ = e;
    update();
}

void Timeline::clear_loop() { set_loop(-1, -1); }

QRectF Timeline::groove() const { return QRectF(10, height() / 2.0 - 11, width() - 20, 22); }

double Timeline::x_of(double edge) const {
    const QRectF g = groove();
    return count_ ? g.left() + g.width() * edge / count_ : g.left();
}

int Timeline::frame_at(double x) const {
    if (count_ <= 0) return 0;
    const QRectF g = groove();
    const int f = static_cast<int>(std::floor((x - g.left()) / g.width() * count_));
    return std::clamp(f, 0, count_ - 1);
}

void Timeline::paintEvent(QPaintEvent*) {
    QPainter p(this);
    const QRectF g = groove();
    p.fillRect(g, palette().color(QPalette::Mid));
    if (count_ <= 0) return;

    if (loop_start_ >= 0) {
        const double x0 = x_of(loop_start_), x1 = x_of(loop_end_ + 1);
        p.fillRect(QRectF(x0, g.top(), std::max(2.0, x1 - x0), g.height()),
                   QColor(255, 170, 0, 190));
    }
    if (start_ >= 0) {
        p.setPen(QPen(QColor(40, 200, 90), 3));
        const double x = x_of(start_);
        p.drawLine(QPointF(x, g.top() - 4), QPointF(x, g.bottom() + 4));
    }
    const double px = x_of(current_ + 0.5);
    p.setPen(QPen(palette().color(QPalette::Highlight), 2));
    p.drawLine(QPointF(px, g.top() - 6), QPointF(px, g.bottom() + 6));
}

void Timeline::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && count_ > 0) emit frameRequested(frame_at(e->position().x()));
}

void Timeline::mouseMoveEvent(QMouseEvent* e) {
    if ((e->buttons() & Qt::LeftButton) && count_ > 0) emit frameRequested(frame_at(e->position().x()));
}

}  // namespace dfd
