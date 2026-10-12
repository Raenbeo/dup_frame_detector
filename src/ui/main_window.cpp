#include "ui/main_window.hpp"

#include "ui/timeline.hpp"
#include "ui/video_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dfd {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("dup_frame_detector");
    resize(1280, 800);
    build_ui();
    play_timer_.setTimerType(Qt::PreciseTimer);
    connect(&play_timer_, &QTimer::timeout, this, &MainWindow::tick);
    update_info();
    update_enabled();
}

MainWindow::~MainWindow() {
    worker_.request_stop();
    worker_.join();
}

void MainWindow::build_ui() {
    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    view_ = new VideoView;
    root->addWidget(view_, 1);

    timeline_ = new Timeline;
    root->addWidget(timeline_);

    // frame / S row
    auto* row1 = new QHBoxLayout;
    open_btn_ = new QPushButton(tr("열기..."));
    path_edit_ = new QLineEdit;
    path_edit_->setPlaceholderText(tr("파일 경로 (비어 있으면 탐색기)"));
    path_edit_->setMinimumWidth(240);
    frame_spin_ = new QSpinBox;
    frame_spin_->setEnabled(false);
    info_ = new QLabel;
    set_start_btn_ = new QPushButton(tr("현재 프레임을 S로"));
    start_label_ = new QLabel(tr("S: 없음"));
    row1->addWidget(open_btn_);
    row1->addWidget(path_edit_);
    row1->addWidget(new QLabel(tr("프레임")));
    row1->addWidget(frame_spin_);
    row1->addWidget(info_, 1);
    row1->addWidget(set_start_btn_);
    row1->addWidget(start_label_);
    root->addLayout(row1);

    // threshold + brush row
    auto* row2 = new QHBoxLayout;
    metric_combo_ = new QComboBox;
    metric_combo_->addItem(tr("픽셀 차이 (MAE)"), static_cast<int>(Metric::Mae));
    metric_combo_->setItemData(0, tr("픽셀 값이 거의 같아야 일치. 무손실/고화질 영상에 적합"),
                               Qt::ToolTipRole);
    metric_combo_->addItem(tr("구조 (SSIM)"), static_cast<int>(Metric::Ssim));
    metric_combo_->setItemData(1, tr("모양·윤곽 기준. 압축 노이즈, 블러가 있는 영상에 적합"),
                               Qt::ToolTipRole);
    metric_combo_->addItem(tr("밝기 무관 (NCC)"), static_cast<int>(Metric::Ncc));
    metric_combo_->setItemData(2, tr("밝기·대비 변화 무시. 페이드, 자동 노출 변화가 있는 영상에 적합"),
                               Qt::ToolTipRole);
    row2->addWidget(new QLabel(tr("비교 방식")));
    row2->addWidget(metric_combo_);
    row2->addSpacing(12);
    threshold_slider_ = new QSlider(Qt::Horizontal);
    threshold_slider_->setRange(0, 1000);  // 0.1% steps
    threshold_slider_->setValue(995);
    threshold_slider_->setMinimumWidth(200);
    threshold_label_ = new QLabel;
    row2->addWidget(new QLabel(tr("일치도")));
    row2->addWidget(threshold_slider_);
    row2->addWidget(threshold_label_);
    row2->addSpacing(24);

    brush_btn_ = new QToolButton;
    brush_btn_->setText(tr("브러시"));
    brush_btn_->setCheckable(true);
    brush_btn_->setChecked(true);
    eraser_btn_ = new QToolButton;
    eraser_btn_->setText(tr("지우개"));
    eraser_btn_->setCheckable(true);
    brush_slider_ = new QSlider(Qt::Horizontal);
    brush_slider_->setRange(1, 200);
    brush_slider_->setValue(view_->brush_radius());
    brush_slider_->setMinimumWidth(120);
    brush_label_ = new QLabel;
    clear_mask_btn_ = new QPushButton(tr("마스크 지우기"));
    row2->addWidget(brush_btn_);
    row2->addWidget(eraser_btn_);
    row2->addWidget(new QLabel(tr("크기")));
    row2->addWidget(brush_slider_);
    row2->addWidget(brush_label_);
    row2->addWidget(clear_mask_btn_);
    row2->addStretch(1);
    root->addLayout(row2);

    // search row
    auto* row3 = new QHBoxLayout;
    search_btn_ = new QPushButton(tr("탐색"));
    cancel_btn_ = new QPushButton(tr("취소"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setTextVisible(true);
    result_ = new QLabel;
    play_btn_ = new QPushButton(tr("구간 반복 재생"));
    play_btn_->setCheckable(true);
    row3->addWidget(search_btn_);
    row3->addWidget(cancel_btn_);
    row3->addWidget(progress_, 1);
    row3->addWidget(result_, 2);
    row3->addWidget(play_btn_);
    export_btn_ = new QPushButton(tr("구간 추출 (무손실)"));
    export_btn_->setToolTip(tr("FFV1 / MKV · 원본 픽셀 포맷 그대로 · 디코드 결과가 원본과 비트 단위로 동일"));
    row3->addWidget(export_btn_);
    root->addLayout(row3);

    setCentralWidget(central);

    connect(open_btn_, &QPushButton::clicked, this, [this] {
        const QString typed = path_edit_->text().trimmed();
        if (!typed.isEmpty()) {
            open_video(typed);
            return;
        }
        const QString p = QFileDialog::getOpenFileName(
            this, tr("영상 열기"), {}, tr("영상 (*.mp4 *.mkv *.mov *.avi *.webm *.gif);;모든 파일 (*)"));
        if (!p.isEmpty()) open_video(p);
    });
    connect(frame_spin_, &QSpinBox::valueChanged, this, &MainWindow::scrub);
    connect(timeline_, &Timeline::frameRequested, this, &MainWindow::scrub);
    connect(set_start_btn_, &QPushButton::clicked, this, &MainWindow::set_start_from_current);
    connect(threshold_slider_, &QSlider::valueChanged, this, [this] {
        update_info();
        invalidate_loop();
    });
    connect(metric_combo_, &QComboBox::currentIndexChanged, this, [this] {
        set_threshold(default_threshold(metric()));  // metrics use different scales
        invalidate_loop();
    });
    connect(brush_btn_, &QToolButton::clicked, this, [this] {
        brush_btn_->setChecked(true);
        eraser_btn_->setChecked(false);
        view_->set_eraser(false);
    });
    connect(eraser_btn_, &QToolButton::clicked, this, [this] {
        eraser_btn_->setChecked(true);
        brush_btn_->setChecked(false);
        view_->set_eraser(true);
    });
    connect(brush_slider_, &QSlider::valueChanged, this, [this](int v) {
        view_->set_brush_radius(v);
        update_info();
    });
    connect(clear_mask_btn_, &QPushButton::clicked, view_, &VideoView::clear_mask);
    connect(view_, &VideoView::maskChanged, this, [this] {
        invalidate_loop();
        update_enabled();
    });
    connect(search_btn_, &QPushButton::clicked, this, &MainWindow::start_search);
    connect(cancel_btn_, &QPushButton::clicked, this, &MainWindow::cancel_search);
    connect(play_btn_, &QPushButton::clicked, this, &MainWindow::set_playing);
    connect(export_btn_, &QPushButton::clicked, this, [this] { export_loop(); });

    auto* left = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(left, &QShortcut::activated, this, [this] { scrub(cur_ - 1); });
    auto* right = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(right, &QShortcut::activated, this, [this] { scrub(cur_ + 1); });
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, [this] {
        if (loop_) set_playing(!playing());
    });
}

Metric MainWindow::metric() const {
    return static_cast<Metric>(metric_combo_->currentData().toInt());
}

void MainWindow::set_metric(Metric m) {
    metric_combo_->setCurrentIndex(metric_combo_->findData(static_cast<int>(m)));
}

double MainWindow::threshold() const { return threshold_slider_->value() / 1000.0; }

void MainWindow::set_threshold(double t) {
    threshold_slider_->setValue(static_cast<int>(std::lround(std::clamp(t, 0.0, 1.0) * 1000)));
}

bool MainWindow::open_video(const QString& path) {
    if (busy()) return false;
    set_playing(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = dec_.open(path.toStdString());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        count_ = 0;
        path_.clear();
        view_->clear_frame();
        timeline_->set_frame_count(0);
        result_->setText(tr("영상을 열 수 없습니다: %1").arg(path));
        update_enabled();
        update_info();
        return false;
    }
    path_ = path;
    count_ = dec_.frame_count();
    dec_next_ = 0;
    cur_ = 0;
    start_ = -1;
    loop_.reset();
    view_->clear_frame();  // drops the old mask
    timeline_->set_frame_count(count_);
    start_label_->setText(tr("S: 없음"));
    result_->clear();
    progress_->setValue(0);
    frame_spin_->blockSignals(true);
    frame_spin_->setRange(0, count_ - 1);
    frame_spin_->blockSignals(false);
    setWindowTitle(QStringLiteral("%1 - dup_frame_detector").arg(QFileInfo(path).fileName()));
    show_frame(0);
    update_enabled();
    return true;
}

void MainWindow::show_frame(int index) {
    if (count_ <= 0) return;
    index = std::clamp(index, 0, count_ - 1);
    if (index != dec_next_ && !dec_.seek(index)) {
        dec_next_ = -1;
        return;
    }
    const cv::Mat f = dec_.next();
    if (f.empty()) {
        dec_next_ = -1;
        return;
    }
    dec_next_ = index + 1;
    cur_ = index;
    view_->set_frame(f);
    view_->set_in_loop(loop_ && index >= loop_->start && index <= loop_->end);
    timeline_->set_current(index);
    frame_spin_->blockSignals(true);
    frame_spin_->setValue(index);
    frame_spin_->blockSignals(false);
    update_info();
}

void MainWindow::scrub(int frame) {
    if (count_ <= 0) return;
    set_playing(false);
    show_frame(frame);
}

void MainWindow::set_start_from_current() {
    if (count_ <= 0 || busy()) return;
    start_ = cur_;
    timeline_->set_start(start_);
    start_label_->setText(tr("S: %1").arg(start_));
    invalidate_loop();
    update_enabled();
}

void MainWindow::invalidate_loop() {
    if (!loop_) return;
    set_playing(false);
    loop_.reset();
    timeline_->clear_loop();
    view_->set_in_loop(false);
    result_->clear();
    update_enabled();
}

void MainWindow::start_search() {
    if (busy() || count_ <= 0 || start_ < 0) return;
    set_playing(false);
    invalidate_loop();
    worker_ = {};  // previous worker has already finished

    searching_ = true;
    progress_->setValue(0);
    result_->setText(tr("탐색 중..."));
    update_enabled();

    LoopOptions opts;
    opts.metric = metric();
    opts.threshold = threshold();
    const std::string path = path_.toStdString();
    const int start = start_;
    const cv::Mat mask = view_->include_mask();  // deep copy: later edits don't affect the search

    worker_ = std::jthread([this, path, start, mask, opts](std::stop_token st) mutable {
        LoopResult res;
        Decoder dec;  // decoders are not shared between threads
        if (dec.open(path)) {
            opts.stop = st;
            int last = -1;
            opts.progress = [this, &last](int done, int total) {
                const int p = total > 0 ? static_cast<int>(1000LL * done / total) : 0;
                if (p == last) return;
                last = p;
                QMetaObject::invokeMethod(
                    this, [this, p] { progress_->setValue(p); }, Qt::QueuedConnection);
            };
            res = find_loop(dec, start, mask, opts);
        }
        QMetaObject::invokeMethod(
            this, [this, res] { on_search_done(res); }, Qt::QueuedConnection);
    });
}

void MainWindow::cancel_search() {
    if (busy()) worker_.request_stop();
}

void MainWindow::export_loop(const QString& path_arg) {
    if (busy() || !loop_) return;
    QString out = path_arg;
    if (out.isEmpty()) {
        const QFileInfo src(path_);
        const QString def = QDir(src.absolutePath())
                                .filePath(QStringLiteral("%1_loop_%2-%3.mkv")
                                              .arg(src.completeBaseName())
                                              .arg(loop_->start)
                                              .arg(loop_->end));
        out = QFileDialog::getSaveFileName(this, tr("구간 추출 (무손실)"), def,
                                           tr("Matroska (*.mkv)"));
        if (out.isEmpty()) return;
        if (QFileInfo(out).suffix().isEmpty()) out += QStringLiteral(".mkv");
    }
    if (QFileInfo(out).absoluteFilePath() == QFileInfo(path_).absoluteFilePath()) {
        result_->setText(tr("원본 파일을 덮어쓸 수 없습니다"));
        return;
    }

    set_playing(false);
    worker_ = {};  // previous worker has already finished
    exporting_ = true;
    progress_->setValue(0);
    result_->setText(tr("추출 중..."));
    update_enabled();

    const std::string src = path_.toStdString();
    const LoopRange range = *loop_;
    worker_ = std::jthread([this, src, range, out](std::stop_token st) {
        ExportResult res{ExportStatus::Error, "cannot open video"};
        Decoder dec;  // decoders are not shared between threads
        if (dec.open(src)) {
            ExportOptions opts;
            opts.stop = st;
            int last = -1;
            opts.progress = [this, &last](int done, int total) {
                const int p = total > 0 ? static_cast<int>(1000LL * done / total) : 0;
                if (p == last) return;
                last = p;
                QMetaObject::invokeMethod(
                    this, [this, p] { progress_->setValue(p); }, Qt::QueuedConnection);
            };
            res = dec.export_lossless(range.start, range.end, out.toStdString(), opts);
        }
        QMetaObject::invokeMethod(
            this, [this, res, out] { on_export_done(res, out); }, Qt::QueuedConnection);
    });
}

void MainWindow::on_export_done(const ExportResult& r, const QString& path) {
    exporting_ = false;
    QString msg;
    switch (r.status) {
    case ExportStatus::Ok:
        progress_->setValue(1000);
        msg = tr("추출 완료: %1").arg(path);
        break;
    case ExportStatus::Cancelled:
        msg = tr("추출 취소됨");
        break;
    case ExportStatus::Error:
        msg = tr("추출 실패: %1").arg(QString::fromStdString(r.error));
        break;
    }
    result_->setText(msg);
    update_enabled();
    emit exportFinished(r.status == ExportStatus::Ok, msg);
}

void MainWindow::on_search_done(const LoopResult& r) {
    searching_ = false;
    switch (r.status) {
    case LoopStatus::Found:
        loop_ = r.range;
        progress_->setValue(1000);
        timeline_->set_loop(r.range.start, r.range.end);
        result_->setText(tr("반복 구간 [%1, %2] · %3프레임 · 유사도 %4%")
                             .arg(r.range.start)
                             .arg(r.range.end)
                             .arg(r.range.end - r.range.start + 1)
                             .arg(r.similarity * 100, 0, 'f', 2));
        show_frame(r.range.start);
        break;
    case LoopStatus::NotFound:
        progress_->setValue(1000);
        result_->setText(tr("반복 없음"));
        break;
    case LoopStatus::Cancelled:
        result_->setText(tr("취소됨"));
        break;
    case LoopStatus::Error:
        result_->setText(tr("오류: 탐색 실패"));
        break;
    }
    update_enabled();
    emit searchFinished(r.status);
}

void MainWindow::set_playing(bool playing) {
    playing = playing && loop_ && !busy();
    play_btn_->setChecked(playing);
    if (!playing) {
        play_timer_.stop();
        return;
    }
    if (cur_ < loop_->start || cur_ > loop_->end) show_frame(loop_->start);
    const double fps = dec_.fps() > 0 ? dec_.fps() : 30.0;
    play_timer_.start(std::max(1, static_cast<int>(std::lround(1000.0 / fps))));
}

void MainWindow::tick() {
    if (!loop_) return set_playing(false);
    show_frame(cur_ >= loop_->end ? loop_->start : cur_ + 1);
}

void MainWindow::update_info() {
    threshold_label_->setText(QStringLiteral("%1%").arg(threshold() * 100, 0, 'f', 1));
    brush_label_->setText(QStringLiteral("%1px").arg(brush_slider_->value()));
    if (count_ <= 0) {
        info_->setText(tr("영상 없음"));
        return;
    }
    const double fps = dec_.fps();
    info_->setText(QStringLiteral("%1 / %2   %3 s")
                       .arg(cur_)
                       .arg(count_ - 1)
                       .arg(fps > 0 ? cur_ / fps : 0.0, 0, 'f', 3));
}

void MainWindow::update_enabled() {
    const bool loaded = count_ > 0;
    const bool idle = !busy();
    open_btn_->setEnabled(idle);
    frame_spin_->setEnabled(loaded);
    set_start_btn_->setEnabled(loaded && idle);
    metric_combo_->setEnabled(idle);
    threshold_slider_->setEnabled(idle);
    brush_btn_->setEnabled(idle);
    eraser_btn_->setEnabled(idle);
    brush_slider_->setEnabled(idle);
    clear_mask_btn_->setEnabled(idle && view_->has_mask());
    search_btn_->setEnabled(loaded && idle && start_ >= 0);
    cancel_btn_->setEnabled(busy());
    export_btn_->setEnabled(loop_.has_value() && idle);
    play_btn_->setEnabled(loop_.has_value() && idle);
}

}  // namespace dfd
