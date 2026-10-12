#include "ui/main_window.hpp"

#include "ui/timeline.hpp"
#include "ui/video_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QHBoxLayout>
#include <QImage>
#include <QFile>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dfd {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("dup_frame_detector");
    setAcceptDrops(true);
    resize(1280, 800);
    build_ui();
    play_timer_.setTimerType(Qt::PreciseTimer);
    connect(&play_timer_, &QTimer::timeout, this, &MainWindow::tick);
    update_info();
    update_format_ui();
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
    next_btn_ = new QPushButton(tr("다음 구간 탐색"));
    next_btn_->setToolTip(tr("현재 반복 구간의 반복이 끝난 뒤, 같은 구간이 다시 나타나는 곳을 찾음"));
    cancel_btn_ = new QPushButton(tr("취소"));
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setTextVisible(true);
    result_ = new QLabel;
    play_btn_ = new QPushButton(tr("구간 반복 재생"));
    play_btn_->setCheckable(true);
    row3->addWidget(search_btn_);
    row3->addWidget(next_btn_);
    row3->addWidget(cancel_btn_);
    row3->addWidget(progress_, 1);
    row3->addWidget(result_, 2);
    row3->addWidget(play_btn_);
    export_btn_ = new QPushButton(tr("구간 추출 (무손실)"));
    export_btn_->setToolTip(tr("FFV1 / MKV · 원본 픽셀 포맷 그대로 · 디코드 결과가 원본과 비트 단위로 동일"));
    row3->addWidget(export_btn_);
    root->addLayout(row3);

    // frame save row
    auto* row4 = new QHBoxLayout;
    format_label_ = new QLabel;
    save_native_btn_ = new QPushButton(tr("원본 저장 (PNG in YUV)"));
    save_png_btn_ = new QPushButton(tr("PNG로 저장"));
    row4->addWidget(format_label_, 1);
    row4->addWidget(save_native_btn_);
    row4->addWidget(save_png_btn_);
    help_btn_ = new QPushButton(tr("도움말"));
    help_btn_->setToolTip(tr("사용법과 단축키 (F1)"));
    row4->addWidget(help_btn_);
    root->addLayout(row4);

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
    connect(next_btn_, &QPushButton::clicked, this, &MainWindow::start_next_search);
    connect(cancel_btn_, &QPushButton::clicked, this, &MainWindow::cancel_search);
    connect(play_btn_, &QPushButton::clicked, this, &MainWindow::set_playing);
    connect(export_btn_, &QPushButton::clicked, this, [this] { export_loop(); });
    connect(save_native_btn_, &QPushButton::clicked, this, [this] { save_frame_native(); });
    connect(save_png_btn_, &QPushButton::clicked, this, [this] { save_frame_png(); });

    auto* left = new QShortcut(QKeySequence(Qt::Key_Left), this);
    connect(left, &QShortcut::activated, this, [this] { scrub(cur_ - 1); });
    auto* right = new QShortcut(QKeySequence(Qt::Key_Right), this);
    connect(right, &QShortcut::activated, this, [this] { scrub(cur_ + 1); });
    const std::pair<QKeyCombination, int> steps[] = {
        {Qt::Key_W, 1},
        {Qt::SHIFT | Qt::Key_W, 5},
        {Qt::Key_B, -1},
        {Qt::SHIFT | Qt::Key_B, -5},
    };
    for (const auto& [key, step] : steps) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        connect(sc, &QShortcut::activated, this, [this, step] { scrub(cur_ + step); });
    }
    connect(help_btn_, &QPushButton::clicked, this, &MainWindow::show_help);
    auto* help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, &MainWindow::show_help);
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, [this] {
        if (loop_) set_playing(!playing());
    });
}

void MainWindow::show_help() {
    QMessageBox box(this);
    box.setWindowTitle(tr("도움말"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>반복 구간 찾기</h3>"
        "<p>기준 프레임 <b>S</b>를 고르면 S+1부터 차례로 비교해서 S와 같은 프레임 <b>E</b>를 찾습니다. "
        "반복 구간은 <b>[S, E-1]</b>입니다.</p>"
        "<h4>사용 순서</h4>"
        "<ol>"
        "<li><b>열기...</b>로 영상을 엽니다. 경로 칸에 직접 입력해도 됩니다.</li>"
        "<li>원하는 프레임으로 이동한 뒤 <b>현재 프레임을 S로</b>를 누릅니다.</li>"
        "<li>자막, 시계처럼 바뀌는 부분은 화면에 <b>브러시</b>로 칠해 비교에서 뺍니다.</li>"
        "<li><b>비교 방식</b>과 <b>일치도</b>를 고른 뒤 <b>탐색</b>을 누릅니다.</li>"
        "<li>찾으면 타임라인에 구간이 표시되고 <b>구간 반복 재생</b>, <b>구간 추출</b>을 쓸 수 있습니다.</li>"
        "</ol>"
        "<h4>비교 방식</h4>"
        "<ul>"
        "<li><b>MAE</b>: 픽셀 값 차이. 무손실/고화질 영상</li>"
        "<li><b>SSIM</b>: 구조 기준. 압축 노이즈, 블러가 있는 영상</li>"
        "<li><b>NCC</b>: 밝기·대비 변화 무시. 페이드, 자동 노출</li>"
        "</ul>"
        "<p>방식을 바꾸면 일치도가 그 방식의 기본값으로 돌아갑니다. "
        "못 찾으면 일치도를 낮추고, 엉뚱한 곳에서 멈추면 높이세요.</p>"
        "<h4>단축키</h4>"
        "<table cellpadding='2'>"
        "<tr><td><b>W</b> / <b>→</b></td><td>다음 프레임</td></tr>"
        "<tr><td><b>B</b> / <b>←</b></td><td>이전 프레임</td></tr>"
        "<tr><td><b>Shift+W</b></td><td>5프레임 앞으로</td></tr>"
        "<tr><td><b>Shift+B</b></td><td>5프레임 뒤로</td></tr>"
        "<tr><td><b>Space</b></td><td>구간 반복 재생 / 정지</td></tr>"
        "<tr><td><b>F1</b></td><td>도움말</td></tr>"
        "</table>"));
    box.exec();
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

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() && !busy()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) return;
    event->acceptProposedAction();
    open_video(urls.first().toLocalFile());
}

bool MainWindow::open_video(const QString& path) {
    if (busy()) return false;
    set_playing(false);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = dec_.open(path.toStdString());
    QApplication::restoreOverrideCursor();
    if (!ok) {
        count_ = 0;
        cur_bgr_.release();
        pix_info_ = {};
        path_.clear();
        view_->clear_frame();
        timeline_->set_frame_count(0);
        result_->setText(tr("영상을 열 수 없습니다: %1").arg(path));
        update_enabled();
        update_info();
        update_format_ui();
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
    cur_bgr_ = f;
    pix_info_ = dec_.pixel_info();
    view_->set_frame(f);
    view_->set_in_loop(loop_ && index >= loop_->start && index <= loop_->end);
    timeline_->set_current(index);
    update_format_ui();
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
    run_search(std::nullopt);
}

void MainWindow::start_next_search() {
    if (busy() || count_ <= 0 || !loop_) return;
    run_search(loop_);
}

void MainWindow::run_search(std::optional<LoopRange> prev) {
    set_playing(false);
    if (!prev) invalidate_loop();  // a next-loop search keeps the current loop until it finds one
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
    const bool next = prev.has_value();
    const LoopRange prev_range = prev.value_or(LoopRange{});
    const cv::Mat mask = view_->include_mask();  // deep copy: later edits don't affect the search

    worker_ = std::jthread([this, path, start, next, prev_range, mask, opts](std::stop_token st) mutable {
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
            res = next ? find_next_loop(dec, prev_range, mask, opts)
                       : find_loop(dec, start, mask, opts);
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

bool MainWindow::save_frame_png(const QString& path_arg) {
    if (busy() || count_ <= 0 || cur_bgr_.empty()) return false;
    QString out = path_arg;
    if (out.isEmpty()) {
        const QFileInfo src(path_);
        const QString def = QDir(src.absolutePath())
                                .filePath(QStringLiteral("%1_frame_%2.png")
                                              .arg(src.completeBaseName())
                                              .arg(cur_));
        out = QFileDialog::getSaveFileName(this, tr("현재 프레임 PNG 저장"), def, tr("PNG (*.png)"));
        if (out.isEmpty()) return false;
        if (QFileInfo(out).suffix().isEmpty()) out += QStringLiteral(".png");
    }
    // Wraps the BGR bytes without conversion; PNG is lossless, so pixels are stored exactly.
    const QImage img(cur_bgr_.data, cur_bgr_.cols, cur_bgr_.rows,
                     static_cast<qsizetype>(cur_bgr_.step[0]), QImage::Format_BGR888);
    const bool ok = img.save(out, "PNG", 0);
    result_->setText(ok ? tr("프레임 %1 저장 완료: %2").arg(cur_).arg(out)
                        : tr("PNG 저장 실패: %1").arg(out));
    return ok;
}

bool MainWindow::save_frame_native(const QString& meta_arg) {
    if (busy() || count_ <= 0 || dec_next_ != cur_ + 1) return false;  // decoder must hold cur_
    const NativeFrame nf = dec_.native_frame();
    if (nf.planes.empty()) {
        result_->setText(tr("이 픽셀 포맷은 원본 저장을 지원하지 않습니다: %1")
                             .arg(QString::fromStdString(nf.info.pix_fmt)));
        return false;
    }
    QString meta = meta_arg;
    if (meta.isEmpty()) {
        const QFileInfo src(path_);
        const QString def = QDir(src.absolutePath())
                                .filePath(QStringLiteral("%1_frame_%2_%3.txt")
                                              .arg(src.completeBaseName())
                                              .arg(cur_)
                                              .arg(QString::fromStdString(nf.info.pix_fmt)));
        meta = QFileDialog::getSaveFileName(this, tr("원본 저장 (평면별 PNG + 설명 txt)"), def,
                                            tr("원본 저장 (*.txt)"));
        if (meta.isEmpty()) return false;
        if (QFileInfo(meta).suffix().isEmpty()) meta += QStringLiteral(".txt");
    }
    const QFileInfo mi(meta);
    const QString stem = mi.dir().filePath(mi.completeBaseName());

    QString text;
    QTextStream ts(&text);
    ts << "pix_fmt=" << QString::fromStdString(nf.info.pix_fmt) << "\n"
       << "frame=" << cur_ << "\n"
       << "width=" << cur_bgr_.cols << "\n"
       << "height=" << cur_bgr_.rows << "\n"
       << "bit_depth=" << nf.info.bit_depth << "\n"
       << "color_range=" << QString::fromStdString(nf.info.color_range) << "\n"
       << "colorspace=" << QString::fromStdString(nf.info.colorspace) << "\n"
       << "# each plane: 8-bit gray PNG, or 16-bit gray PNG holding the raw (unshifted) sample\n";

    bool ok = true;
    for (size_t i = 0; ok && i < nf.planes.size(); ++i) {
        const cv::Mat& m = nf.planes[i];
        const QString file = QStringLiteral("%1_%2.png")
                                 .arg(stem, QString::fromStdString(nf.plane_names[i]));
        const QImage img(m.data, m.cols, m.rows, static_cast<qsizetype>(m.step[0]),
                         nf.info.bit_depth > 8 ? QImage::Format_Grayscale16
                                               : QImage::Format_Grayscale8);
        ok = img.save(file, "PNG");
        ts << "plane_" << QString::fromStdString(nf.plane_names[i]) << "=" << QFileInfo(file).fileName()
           << " " << m.cols << "x" << m.rows << "\n";
    }
    if (ok) {
        QFile f(meta);
        ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text.toUtf8()) >= 0;
    }
    result_->setText(ok ? tr("프레임 %1 원본 저장 완료: %2_*.png").arg(cur_).arg(stem)
                        : tr("원본 저장 실패: %1").arg(stem));
    return ok;
}

void MainWindow::update_format_ui() {
    const PixelInfo& p = pix_info_;
    auto emphasize = [](QPushButton* b, bool on) {
        QFont f = b->font();
        f.setBold(on);
        b->setFont(f);
    };
    if (!p.valid) {
        format_label_->setText(tr("픽셀 포맷: -"));
        emphasize(save_native_btn_, false);
        emphasize(save_png_btn_, false);
        return;
    }
    QString rec;
    if (p.png_exact) {
        rec = tr("권장: PNG로 저장 (원본이 RGB 8bit라 손실 없음)");
    } else if (p.planar_saveable) {
        rec = tr("권장: 원본 저장 (PNG는 YUV→RGB 변환본이라 원본과 다름)");
    } else {
        rec = tr("원본 저장 미지원 포맷 (PNG는 변환본)");
    }
    format_label_->setText(tr("픽셀 포맷: %1 · %2bit · %3 · %4  →  %5")
                               .arg(QString::fromStdString(p.pix_fmt))
                               .arg(p.bit_depth)
                               .arg(QString::fromStdString(p.color_range),
                                    QString::fromStdString(p.colorspace), rec));
    emphasize(save_native_btn_, !p.png_exact && p.planar_saveable);
    emphasize(save_png_btn_, p.png_exact);
    save_png_btn_->setToolTip(p.png_exact
                                  ? tr("원본 픽셀과 동일한 무손실 PNG")
                                  : tr("RGB로 변환한 PNG. 파일은 무손실이지만 변환 과정에서 원본과 달라질 수 있음. 마스크 미포함"));
    save_native_btn_->setToolTip(tr("디코더 출력 평면(Y/U/V 등)을 변환 없이 평면별 PNG + 설명 txt로 저장"));
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
        start_ = r.range.start;
        timeline_->set_start(start_);
        start_label_->setText(tr("S: %1").arg(start_));
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
        result_->setText(tr(loop_ ? "다음 구간 없음" : "반복 없음"));
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
    const auto fmt = [fps](int frame) {
        const qint64 ms = fps > 0 ? std::llround(frame * 1000.0 / fps) : 0;
        return QStringLiteral("%1:%2.%3")
            .arg(ms / 60000, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    };
    info_->setText(QStringLiteral("%1 / %2   %3 / %4")
                       .arg(cur_)
                       .arg(count_ - 1)
                       .arg(fmt(cur_), fmt(count_ - 1)));
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
    next_btn_->setEnabled(loop_.has_value() && idle);
    cancel_btn_->setEnabled(busy());
    export_btn_->setEnabled(loop_.has_value() && idle);
    save_png_btn_->setEnabled(loaded && idle);
    save_native_btn_->setEnabled(loaded && idle && pix_info_.planar_saveable);
    play_btn_->setEnabled(loop_.has_value() && idle);
}

}  // namespace dfd
