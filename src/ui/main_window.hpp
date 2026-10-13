#pragma once

#include "decoder/decoder.hpp"
#include "matcher/matcher.hpp"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>
#include <thread>

class QComboBox;
class QEvent;
class QLabel;
class QProgressBar;
class QPushButton;
class QLineEdit;
class QSlider;
class QSplitter;
class QSpinBox;
class QToolButton;

namespace dfd {

class Timeline;
class VideoView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;

public:

    bool open_video(const QString& path);
    void show_frame(int index);
    void set_start_from_current();
    void start_search();
    void start_next_search();  // searches for the loop's next occurrence after the current loop
    void cancel_search();
    // Lossless (FFV1/MKV) export of the found loop. Empty path -> ask with a dialog.
    void export_loop(const QString& path = {});
    // Saves the currently shown frame as PNG (lossless). Empty path -> ask with a dialog.
    bool save_frame_png(const QString& path = {});
    // Saves the shown frame as a colour PNG converted straight from the decoded frame
    // (Decoder::precise_rgb, 16-bit for deep sources). Empty path -> ask with a dialog.
    bool save_frame_native(const QString& path = {});
    const PixelInfo& pixel_info() const { return pix_info_; }
    void set_playing(bool playing);

    int current_frame() const { return cur_; }
    int loop_frame() const { return loop_cur_; }  // frame shown in the right-hand loop view
    int start_frame() const { return start_; }
    std::optional<LoopRange> loop() const { return loop_; }
    bool searching() const { return searching_; }
    bool exporting() const { return exporting_; }
    bool overlay() const;
    int tint() const { return tint_; }
    void set_overlay(bool on);
    double play_rate() const;  // frames per second at the selected speed
    int play_interval_ms() const;
    bool playing() const { return play_timer_.isActive(); }
    Metric metric() const;
    void set_metric(Metric m);  // also resets the threshold to the metric's default
    double threshold() const;
    void set_threshold(double t);  // [0, 1]
    VideoView* view() const { return view_; }
    VideoView* loop_view() const { return loop_view_; }
    Timeline* timeline() const { return timeline_; }

signals:
    void searchFinished(dfd::LoopStatus status);
    void exportFinished(bool ok, QString message);

private:
    void build_ui();
    void scrub(int frame);
    void show_help();
    void show_loop_frame(int index);
    void cycle_tint();
    void update_tint_text();
    void update_start_label();
    void update_loop_caption();
    void retranslate_ui();  // re-applies every translatable text after a language switch
    void apply_overlay();  // overlay mode: loop frame blended onto the original view
    void invalidate_loop();
    void run_search(std::optional<LoopRange> prev);
    void on_search_done(const LoopResult& r);
    void on_export_done(const ExportResult& r, const QString& path);
    bool busy() const { return searching_ || exporting_; }
    void update_info();
    void update_format_ui();
    // Status text; the label is clipped instead of widening the window, full text in tooltip.
    void show_result(const QString& text);
    void update_enabled();
    void restart_play_clock();
    void tick();

    Decoder dec_;
    Decoder loop_dec_;      // feeds the loop view; independent of dec_ so both play at once
    int loop_dec_next_ = -1;
    int loop_cur_ = -1;
    cv::Mat loop_bgr_;  // pixels of the loop frame currently shown
    QString path_;
    int count_ = 0;
    int cur_ = 0;
    PixelInfo pix_info_;
    cv::Mat cur_bgr_;    // pixels of the frame currently shown
    int dec_next_ = -1;  // frame index the decoder returns on next()
    int start_ = -1;
    std::optional<LoopRange> loop_;
    bool searching_ = false;
    bool exporting_ = false;

    VideoView* view_ = nullptr;
    VideoView* loop_view_ = nullptr;
    QSplitter* split_ = nullptr;
    Timeline* timeline_ = nullptr;
    QSpinBox* frame_spin_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* start_label_ = nullptr;
    QComboBox* metric_combo_ = nullptr;
    QSlider* threshold_slider_ = nullptr;
    QLabel* threshold_label_ = nullptr;
    QToolButton* brush_btn_ = nullptr;
    QToolButton* eraser_btn_ = nullptr;
    QSlider* brush_slider_ = nullptr;
    QLabel* brush_label_ = nullptr;
    QPushButton* open_btn_ = nullptr;
    QLineEdit* path_edit_ = nullptr;
    QPushButton* set_start_btn_ = nullptr;
    QPushButton* clear_mask_btn_ = nullptr;
    QPushButton* search_btn_ = nullptr;
    QPushButton* next_btn_ = nullptr;
    QPushButton* cancel_btn_ = nullptr;
    QPushButton* play_btn_ = nullptr;
    QComboBox* speed_combo_ = nullptr;
    QPushButton* overlay_btn_ = nullptr;
    QSlider* overlay_slider_ = nullptr;
    QPushButton* tint_btn_ = nullptr;
    QPushButton* language_btn_ = nullptr;
    std::vector<std::function<void()>> retranslators_;
    int tint_ = 0;  // index into the colour-filter cycle
    QPushButton* export_btn_ = nullptr;
    QPushButton* save_png_btn_ = nullptr;
    QPushButton* save_native_btn_ = nullptr;
    QPushButton* help_btn_ = nullptr;
    QLabel* format_label_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* result_ = nullptr;
    QTimer play_timer_;
    QElapsedTimer play_clock_;  // wall time since playback (re)started at this speed
    qint64 play_frames_ = 0;    // frames advanced since play_clock_ started

    std::jthread worker_;  // keep last: joined first on destruction
};

}  // namespace dfd
