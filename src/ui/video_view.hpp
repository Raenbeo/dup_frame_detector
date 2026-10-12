#pragma once

#include <opencv2/core.hpp>

#include <QColor>
#include <QImage>
#include <QString>
#include <QPointF>
#include <QWidget>

namespace dfd {

// Shows a video frame (aspect-fit) with an editable exclusion mask on top.
// The mask has the video's resolution; painted pixels are excluded from comparison.
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget* parent = nullptr);

    void set_frame(const cv::Mat& bgr);  // reallocates the mask if the size changed
    void clear_frame();
    // Text shown while no frame is loaded. Empty -> default prompt.
    void set_message(const QString& text);

    // Highlight border: current frame lies inside the found loop.
    void set_in_loop(bool in_loop);
    // Label badge drawn in the top-left corner of the picture. Empty -> none.
    void set_caption(const QString& text, QColor color = QColor(40, 40, 40, 200));

    void set_brush_radius(int radius_px);  // in video pixels
    void set_eraser(bool eraser);
    // Display-only views ignore the mouse (no mask painting, no brush cursor).
    void set_editable(bool editable);
    int brush_radius() const { return radius_; }
    bool eraser() const { return eraser_; }

    void clear_mask();
    bool has_mask() const;
    // Comparison mask for the matcher: 255 = compare, 0 = painted (excluded).
    // Empty if nothing is painted.
    cv::Mat include_mask() const;

    cv::Size video_size() const { return {frame_.width(), frame_.height()}; }
    // Widget position that shows the given video pixel (for tests).
    QPointF widget_pos(QPointF image_pos) const;

signals:
    void maskChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    QRectF image_rect() const;
    QPointF to_image(QPointF widget_pos) const;
    void stroke(QPoint from, QPoint to);
    void reset_mask(cv::Size size);

    QImage frame_;
    QString message_;
    QString caption_;
    QColor caption_color_;
    QImage overlay_;  // ARGB32_Premultiplied, mirrors mask_
    cv::Mat mask_;    // CV_8UC1, 255 = excluded
    int radius_ = 20;
    bool eraser_ = false;
    bool editable_ = true;
    bool in_loop_ = false;
    bool drawing_ = false;
    bool changed_in_stroke_ = false;
    QPoint last_;
    QPointF hover_;
    bool hovering_ = false;
};

}  // namespace dfd
