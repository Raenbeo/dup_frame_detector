#pragma once

#include <QWidget>

namespace dfd {

// Frame scrubber: shows the playhead, reference frame S and the found loop [S, E-1].
class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget* parent = nullptr);

    void set_frame_count(int n);
    void set_current(int frame);
    void set_start(int frame);  // -1 = none
    void set_loop(int start, int end);
    void clear_loop();

    int frame_at(double x) const;
    double x_of(double frame_edge) const;
    QSize sizeHint() const override { return {400, 40}; }

signals:
    void frameRequested(int frame);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;

private:
    QRectF groove() const;

    int count_ = 0;
    int current_ = 0;
    int start_ = -1;
    int loop_start_ = -1, loop_end_ = -1;
};

}  // namespace dfd
