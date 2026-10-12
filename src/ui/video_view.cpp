#include "ui/video_view.hpp"

#include <opencv2/imgproc.hpp>

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

namespace dfd {

namespace {
constexpr QRgb kMaskColor = 0x78780000;  // premultiplied ARGB: red, alpha 120
}

VideoView::VideoView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 180);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoView::set_frame(const cv::Mat& bgr) {
    if (bgr.empty() || bgr.type() != CV_8UC3) return;
    frame_ = QImage(bgr.data, bgr.cols, bgr.rows, static_cast<qsizetype>(bgr.step[0]),
                    QImage::Format_BGR888)
                 .copy();
    if (mask_.size() != bgr.size()) reset_mask(bgr.size());
    update();
}

void VideoView::set_blend_frame(const cv::Mat& bgr) {
    if (!bgr.empty() && bgr.type() != CV_8UC3) return;
    blend_src_ = bgr;
    rebuild_blend();
}

void VideoView::set_blend_tint(QColor tint) {
    blend_tint_ = tint;
    rebuild_blend();
}

void VideoView::rebuild_blend() {
    if (blend_src_.empty()) {
        blend_ = QImage();
    } else {
        cv::Mat src = blend_src_;
        if (blend_tint_ != QColor(Qt::white)) {
            cv::multiply(blend_src_, cv::Scalar(blend_tint_.blue(), blend_tint_.green(), blend_tint_.red()),
                         src, 1.0 / 255.0);
        }
        blend_ = QImage(src.data, src.cols, src.rows, static_cast<qsizetype>(src.step[0]),
                        QImage::Format_BGR888)
                     .copy();
    }
    update();
}

void VideoView::set_blend_opacity(double opacity) {
    blend_opacity_ = std::clamp(opacity, 0.0, 1.0);
    update();
}

void VideoView::set_message(const QString& text) {
    message_ = text;
    update();
}

void VideoView::clear_frame() {
    frame_ = QImage();
    blend_ = QImage();
    blend_src_.release();
    mask_.release();
    overlay_ = QImage();
    in_loop_ = false;
    update();
}

void VideoView::set_in_loop(bool in_loop) {
    if (in_loop_ == in_loop) return;
    in_loop_ = in_loop;
    update();
}

void VideoView::set_caption(const QString& text, QColor color) {
    caption_ = text;
    caption_color_ = color;
    update();
}

void VideoView::set_brush_radius(int r) {
    radius_ = std::max(1, r);
    update();
}

void VideoView::set_eraser(bool eraser) { eraser_ = eraser; }

void VideoView::set_editable(bool editable) {
    editable_ = editable;
    setMouseTracking(editable);
}

void VideoView::reset_mask(cv::Size size) {
    mask_ = cv::Mat::zeros(size, CV_8UC1);
    overlay_ = QImage(size.width, size.height, QImage::Format_ARGB32_Premultiplied);
    overlay_.fill(Qt::transparent);
}

void VideoView::clear_mask() {
    if (mask_.empty() || !has_mask()) return;
    reset_mask(mask_.size());
    update();
    emit maskChanged();
}

bool VideoView::has_mask() const { return !mask_.empty() && cv::countNonZero(mask_) > 0; }

cv::Mat VideoView::include_mask() const {
    if (!has_mask()) return {};
    cv::Mat inc;
    cv::bitwise_not(mask_, inc);
    return inc;
}

QRectF VideoView::image_rect() const {
    if (frame_.isNull()) return {};
    const double scale = std::min(double(width()) / frame_.width(),
                                  double(height()) / frame_.height());
    const double w = frame_.width() * scale, h = frame_.height() * scale;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

QPointF VideoView::to_image(QPointF p) const {
    const QRectF r = image_rect();
    return {(p.x() - r.x()) * frame_.width() / r.width(),
            (p.y() - r.y()) * frame_.height() / r.height()};
}

QPointF VideoView::widget_pos(QPointF ip) const {
    const QRectF r = image_rect();
    return {r.x() + ip.x() * r.width() / frame_.width(),
            r.y() + ip.y() * r.height() / frame_.height()};
}

void VideoView::stroke(QPoint a, QPoint b) {
    const int r = radius_;
    cv::line(mask_, {a.x(), a.y()}, {b.x(), b.y()}, cv::Scalar(eraser_ ? 0 : 255), 2 * r + 1,
             cv::LINE_8);
    const QRect dirty = QRect(a, b).normalized().adjusted(-r - 2, -r - 2, r + 2, r + 2)
                            .intersected(overlay_.rect());
    for (int y = dirty.top(); y <= dirty.bottom(); ++y) {
        const uchar* m = mask_.ptr<uchar>(y);
        auto* o = reinterpret_cast<QRgb*>(overlay_.scanLine(y));
        for (int x = dirty.left(); x <= dirty.right(); ++x) o[x] = m[x] ? kMaskColor : 0;
    }
    changed_in_stroke_ = true;
}

void VideoView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (frame_.isNull()) {
        p.setPen(Qt::gray);
        p.drawText(rect(), Qt::AlignCenter, message_.isEmpty() ? tr("영상을 여세요") : message_);
        return;
    }
    const QRectF r = image_rect();
    p.drawImage(r, frame_);
    if (!blend_.isNull()) {
        p.setOpacity(blend_opacity_);
        p.drawImage(r, blend_);
        p.setOpacity(1.0);
    }
    p.drawImage(r, overlay_);

    if (in_loop_) {
        p.setPen(QPen(QColor(255, 170, 0), 4));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r.adjusted(2, 2, -2, -2));
    }
    if (!caption_.isEmpty()) {
        QFont f = p.font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.2);
        p.setFont(f);
        const QRectF text = QFontMetricsF(f).boundingRect(caption_);
        const QRectF badge(r.left() + 8, r.top() + 8, text.width() + 20, text.height() + 8);
        p.setPen(Qt::NoPen);
        p.setBrush(caption_color_);
        p.drawRoundedRect(badge, 4, 4);
        p.setPen(caption_color_.lightness() > 128 ? Qt::black : Qt::white);
        p.drawText(badge, Qt::AlignCenter, caption_);
    }
    if (hovering_ && r.contains(hover_)) {
        const double rad = radius_ * r.width() / frame_.width();
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::black, 3));
        p.drawEllipse(hover_, rad, rad);
        p.setPen(QPen(eraser_ ? QColor(80, 220, 255) : QColor(255, 255, 255), 1));
        p.drawEllipse(hover_, rad, rad);
    }
}

void VideoView::mousePressEvent(QMouseEvent* e) {
    if (!editable_ || e->button() != Qt::LeftButton || frame_.isNull()) return;
    drawing_ = true;
    changed_in_stroke_ = false;
    last_ = to_image(e->position()).toPoint();
    stroke(last_, last_);
    update();
}

void VideoView::mouseMoveEvent(QMouseEvent* e) {
    if (!editable_) return;
    hover_ = e->position();
    hovering_ = true;
    if (drawing_) {
        const QPoint cur = to_image(e->position()).toPoint();
        stroke(last_, cur);
        last_ = cur;
    }
    update();
}

void VideoView::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !drawing_) return;
    drawing_ = false;
    if (changed_in_stroke_) emit maskChanged();
}

void VideoView::leaveEvent(QEvent*) {
    hovering_ = false;
    update();
}

}  // namespace dfd
