#include "ui/main_window.hpp"

#include "ui/i18n.hpp"
#include "ui/timeline.hpp"
#include "ui/video_view.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
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
#include <QSplitter>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dfd {

namespace {
// mm:ss.mmm for a frame index (or a frame count, i.e. a duration) at the given fps.
QString format_time(int frames, double fps) {
    const qint64 ms = fps > 0 ? std::llround(frames * 1000.0 / fps) : 0;
    return QStringLiteral("%1:%2.%3")
        .arg(ms / 60000, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

// Make `dec` (whose next frame is `next`, -1 if unknown) return `index` next. A short
// gap forward is decoded through, which is cheaper than seeking back to a keyframe.
bool position(Decoder& dec, int next, int index) {
    constexpr int kMaxSkip = 64;
    if (next >= 0 && index > next && index - next <= kMaxSkip) return dec.skip(index - next);
    return dec.seek(index);
}

// `from + step`, cycling from `last` back to `first`.
int wrap_step(int from, int step, int first, int last) {
    const int to = from + step;
    return to <= last ? to : first + (to - last - 1) % (last - first + 1);
}
}  // namespace

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
    // Static texts are registered so a language switch can re-apply them (retranslate_ui).
    auto text = [this](auto* w, const char* src) {
        w->setText(tr(src));
        retranslators_.push_back([this, w, src] { w->setText(tr(src)); });
        return w;
    };
    auto tip = [this](QWidget* w, const char* src) {
        w->setToolTip(tr(src));
        retranslators_.push_back([this, w, src] { w->setToolTip(tr(src)); });
    };

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    // Left: original (plays straight through). Right: found loop only, repeating.
    view_ = new VideoView;
    loop_view_ = new VideoView;
    loop_view_->set_editable(false);
    loop_view_->set_in_loop(true);
    loop_view_->hide();  // shown once a loop is found
    split_ = new QSplitter(Qt::Horizontal);
    split_->setChildrenCollapsible(false);
    split_->addWidget(view_);
    split_->addWidget(loop_view_);
    split_->setStretchFactor(0, 1);
    split_->setStretchFactor(1, 1);
    root->addWidget(split_, 1);

    timeline_ = new Timeline;
    root->addWidget(timeline_);

    // frame / S row
    auto* row1 = new QHBoxLayout;
    open_btn_ = text(new QPushButton, "열기...");
    path_edit_ = new QLineEdit;
    path_edit_->setPlaceholderText(tr("파일 경로 (비어 있으면 탐색기)"));
    retranslators_.push_back([this] { path_edit_->setPlaceholderText(tr("파일 경로 (비어 있으면 탐색기)")); });
    path_edit_->setMinimumWidth(240);
    frame_spin_ = new QSpinBox;
    frame_spin_->setEnabled(false);
    info_ = new QLabel;
    set_start_btn_ = text(new QPushButton, "현재 프레임을 S로");
    start_label_ = new QLabel;  // text set by update_start_label()
    row1->addWidget(open_btn_);
    row1->addWidget(path_edit_);
    row1->addWidget(text(new QLabel, "프레임"));
    row1->addWidget(frame_spin_);
    row1->addWidget(info_, 1);
    row1->addWidget(set_start_btn_);
    row1->addWidget(start_label_);
    root->addLayout(row1);

    // threshold + brush row
    auto* row2 = new QHBoxLayout;
    metric_combo_ = new QComboBox;
    metric_combo_->addItem({}, static_cast<int>(Metric::Mae));
    metric_combo_->addItem({}, static_cast<int>(Metric::Ssim));
    metric_combo_->addItem({}, static_cast<int>(Metric::Ncc));
    const auto metric_texts = [this] {
        metric_combo_->setItemText(0, tr("픽셀 차이 (MAE)"));
        metric_combo_->setItemData(0, tr("픽셀 값이 거의 같아야 일치. 무손실/고화질 영상에 적합"),
                                   Qt::ToolTipRole);
        metric_combo_->setItemText(1, tr("구조 (SSIM)"));
        metric_combo_->setItemData(1, tr("모양·윤곽 기준. 압축 노이즈, 블러가 있는 영상에 적합"),
                                   Qt::ToolTipRole);
        metric_combo_->setItemText(2, tr("밝기 무관 (NCC)"));
        metric_combo_->setItemData(2, tr("밝기·대비 변화 무시. 페이드, 자동 노출 변화가 있는 영상에 적합"),
                                   Qt::ToolTipRole);
    };
    metric_texts();
    retranslators_.push_back(metric_texts);
    row2->addWidget(text(new QLabel, "비교 방식"));
    row2->addWidget(metric_combo_);
    row2->addSpacing(12);
    threshold_slider_ = new QSlider(Qt::Horizontal);
    threshold_slider_->setRange(0, 1000);  // 0.1% steps
    threshold_slider_->setValue(995);
    threshold_slider_->setMinimumWidth(200);
    threshold_label_ = new QLabel;
    row2->addWidget(text(new QLabel, "일치도"));
    row2->addWidget(threshold_slider_);
    row2->addWidget(threshold_label_);
    row2->addSpacing(24);

    brush_btn_ = new QToolButton;
    text(brush_btn_, "브러시");
    brush_btn_->setCheckable(true);
    brush_btn_->setChecked(true);
    eraser_btn_ = new QToolButton;
    text(eraser_btn_, "지우개");
    eraser_btn_->setCheckable(true);
    brush_slider_ = new QSlider(Qt::Horizontal);
    brush_slider_->setRange(1, 200);
    brush_slider_->setValue(view_->brush_radius());
    brush_slider_->setMinimumWidth(120);
    brush_label_ = new QLabel;
    clear_mask_btn_ = text(new QPushButton, "마스크 지우기");
    row2->addWidget(brush_btn_);
    row2->addWidget(eraser_btn_);
    row2->addWidget(text(new QLabel, "크기"));
    row2->addWidget(brush_slider_);
    row2->addWidget(brush_label_);
    row2->addWidget(clear_mask_btn_);
    row2->addStretch(1);
    root->addLayout(row2);

    // search row
    auto* row3 = new QHBoxLayout;
    search_btn_ = text(new QPushButton, "탐색");
    next_btn_ = text(new QPushButton, "E 이후 S 일치 찾기");
    tip(next_btn_, "S는 그대로 두고, 현재 끝(E) 다음 프레임부터 S와 일치하는 프레임을 찾아 구간 끝을 늘림");
    cancel_btn_ = text(new QPushButton, "취소");
    progress_ = new QProgressBar;
    progress_->setRange(0, 1000);
    progress_->setTextVisible(true);
    result_ = new QLabel;
    // Long messages (file paths) must not grow the window and resize the video view.
    result_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    play_btn_ = text(new QPushButton, "구간 반복 재생");
    play_btn_->setCheckable(true);
    row3->addWidget(search_btn_);
    row3->addWidget(next_btn_);
    row3->addWidget(cancel_btn_);
    row3->addWidget(progress_, 1);
    row3->addWidget(result_, 2);
    row3->addWidget(play_btn_);
    speed_combo_ = new QComboBox;
    speed_combo_->setObjectName("speed");
    for (const double speed : {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0})
        speed_combo_->addItem(QStringLiteral("%1x").arg(speed), speed);
    speed_combo_->setCurrentIndex(speed_combo_->findData(1.0));
    tip(speed_combo_, "재생 속도");
    row3->addWidget(speed_combo_);
    export_btn_ = text(new QPushButton, "구간 추출 (무손실)");
    tip(export_btn_, "FFV1 / MKV · 원본 픽셀 포맷 그대로 · 디코드 결과가 원본과 비트 단위로 동일");
    row3->addWidget(export_btn_);
    root->addLayout(row3);

    // frame save row
    auto* row4 = new QHBoxLayout;
    format_label_ = new QLabel;
    format_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    save_native_btn_ = text(new QPushButton, "원본 저장 (고품질 PNG)");
    save_png_btn_ = text(new QPushButton, "PNG로 저장");
    overlay_btn_ = text(new QPushButton, "겹쳐 보기");
    overlay_btn_->setCheckable(true);
    tip(overlay_btn_, "원본 위에 반복 구간 화면을 반투명으로 겹쳐 표시 (O)");
    overlay_slider_ = new QSlider(Qt::Horizontal);
    overlay_slider_->setRange(0, 100);
    overlay_slider_->setValue(50);
    overlay_slider_->setFixedWidth(120);
    tip(overlay_slider_, "겹친 화면의 불투명도");
    row4->addWidget(overlay_btn_);
    tint_btn_ = new QPushButton;  // text set by update_tint_text()
    tip(tint_btn_, "겹친 화면에 색상 필터 적용 (누를 때마다 없음 → 빨강 → 초록 → 파랑 → 시안). "
                   "원본과 다른 색으로 겹쳐 차이를 구분");
    row4->addWidget(overlay_slider_);
    row4->addWidget(tint_btn_);
    row4->addWidget(format_label_, 1);
    row4->addWidget(save_native_btn_);
    row4->addWidget(save_png_btn_);
    language_btn_ = new QPushButton;
    language_btn_->setObjectName("language");  // text set by retranslate_ui(): shows the language to switch to
    language_btn_->setToolTip(QStringLiteral("English / 한국어"));
    row4->addWidget(language_btn_);
    help_btn_ = text(new QPushButton, "도움말");
    tip(help_btn_, "사용법과 단축키 (F1)");
    row4->addWidget(help_btn_);
    root->addLayout(row4);

    setCentralWidget(central);
    retranslate_ui();

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
    connect(speed_combo_, &QComboBox::currentIndexChanged, this, [this] {
        if (!playing()) return;
        play_timer_.setInterval(play_interval_ms());
        restart_play_clock();
    });
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
    connect(overlay_btn_, &QPushButton::clicked, this, &MainWindow::set_overlay);
    connect(tint_btn_, &QPushButton::clicked, this, &MainWindow::cycle_tint);
    connect(overlay_slider_, &QSlider::valueChanged, this,
            [this](int v) { view_->set_blend_opacity(v / 100.0); });
    for (const Qt::Key key : {Qt::Key_O, Qt::Key_Tab}) {
        auto* overlay_key = new QShortcut(QKeySequence(key), this);
        connect(overlay_key, &QShortcut::activated, this, [this] {
            if (loop_) set_overlay(!overlay());
        });
    }
    connect(language_btn_, &QPushButton::clicked, this, [] {
        i18n::set_language(i18n::language() == i18n::Lang::English ? i18n::Lang::Korean
                                                                   : i18n::Lang::English);
    });
    connect(help_btn_, &QPushButton::clicked, this, &MainWindow::show_help);
    auto* help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, &MainWindow::show_help);
    const std::pair<Qt::Key, int> speed_keys[] = {{Qt::Key_F, 1}, {Qt::Key_D, -1}};
    for (const auto& [key, delta] : speed_keys) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        connect(sc, &QShortcut::activated, this, [this, delta] {
            const int i = std::clamp(speed_combo_->currentIndex() + delta, 0, speed_combo_->count() - 1);
            speed_combo_->setCurrentIndex(i);
        });
    }
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, [this] {
        if (loop_) set_playing(!playing());
    });
}

void MainWindow::show_help() {
    QMessageBox box(this);
    box.setWindowTitle(tr("도움말"));
    box.setTextFormat(Qt::RichText);
    QString html;
    const auto add = [&](const char* fragment) { html += tr(fragment); };
    add("<h3>반복 구간 찾기</h3>");
    add("<p>기준 프레임 <b>S</b>를 고르면 S+1부터 차례로 비교해서 S와 같은 프레임 <b>E</b>를 찾습니다. "
        "반복 구간은 <b>[S, E-1]</b>입니다.</p>");
    add("<h4>사용 순서</h4>");
    add("<ol>");
    add("<li><b>열기...</b>로 영상을 엽니다. 경로 칸에 직접 입력하거나 파일을 창에 끌어다 놓아도 됩니다.</li>");
    add("<li>타임라인을 클릭하거나 프레임 칸/단축키로 원하는 프레임에 이동한 뒤 <b>현재 프레임을 S로</b>를 누릅니다.</li>");
    add("<li>자막, 시계처럼 바뀌는 부분은 화면에 <b>브러시</b>로 칠해 비교에서 뺍니다. "
        "<b>지우개</b>로 일부만 지우거나 <b>마스크 지우기</b>로 전부 지웁니다. 마스크를 고치면 이전 결과는 사라집니다.</li>");
    add("<li><b>비교 방식</b>과 <b>일치도</b>를 고른 뒤 <b>탐색</b>을 누릅니다.</li>");
    add("<li>찾으면 타임라인에 구간이 표시되고, 화면이 왼쪽 <b>원본</b>과 오른쪽 <b>반복 구간</b>으로 나뉩니다. "
        "탐색 중에는 <b>취소</b>할 수 있고, 끝까지 없으면 \"반복 없음\"이 표시됩니다.</li>");
    add("</ol>");
    add("<h4>결과 활용</h4>");
    add("<ul>");
    add("<li><b>구간 반복 재생</b>: 원본은 계속 재생되고, 오른쪽 화면은 [S, E-1]만 반복합니다.</li>");
    add("<li><b>겹쳐 보기</b>: 원본 위에 반복 구간 화면을 반투명으로 겹쳐 두 화면의 차이를 확인합니다. "
        "옆 슬라이더로 불투명도를, <b>색상 필터</b> 버튼으로 겹친 화면의 색(빨강/초록/파랑/시안)을 바꿉니다. "
        "다시 누르면 좌우 분할로 돌아갑니다.</li>");
    add("<li><b>E 이후 S 일치 찾기</b>: S는 그대로 두고 E 다음 프레임부터 S와 일치하는 프레임을 더 찾아 구간 끝을 늘립니다.</li>");
    add("<li><b>구간 추출 (무손실)</b>: 찾은 구간을 FFV1/MKV로 저장합니다.</li>");
    add("<li><b>PNG로 저장</b> / <b>원본 저장 (고품질 PNG)</b>: 현재 프레임을 저장합니다. "
        "고품질 쪽은 영상의 색 행렬/범위와 10비트 이상 색심도를 반영합니다.</li>");
    add("</ul>");
    add("<h4>비교 방식</h4>");
    add("<ul>");
    add("<li><b>MAE</b>: 픽셀 값 차이. 무손실/고화질 영상</li>");
    add("<li><b>SSIM</b>: 구조 기준. 압축 노이즈, 블러가 있는 영상</li>");
    add("<li><b>NCC</b>: 밝기·대비 변화 무시. 페이드, 자동 노출</li>");
    add("</ul>");
    add("<p>방식을 바꾸면 일치도가 그 방식의 기본값으로 돌아갑니다. "
        "못 찾으면 일치도를 낮추고, 엉뚱한 곳에서 멈추면 높이세요.</p>");
    add("<h4>단축키</h4>");
    add("<table cellpadding='2'>");
    add("<tr><td><b>W</b> / <b>→</b></td><td>다음 프레임</td></tr>");
    add("<tr><td><b>B</b> / <b>←</b></td><td>이전 프레임</td></tr>");
    add("<tr><td><b>Shift+W</b></td><td>5프레임 앞으로</td></tr>");
    add("<tr><td><b>Shift+B</b></td><td>5프레임 뒤로</td></tr>");
    add("<tr><td><b>Space</b></td><td>구간 반복 재생 / 정지</td></tr>");
    add("<tr><td><b>O</b> / <b>Tab</b></td><td>겹쳐 보기 켜기/끄기</td></tr>");
    add("<tr><td><b>F</b></td><td>재생 속도 한 단계 빠르게</td></tr>");
    add("<tr><td><b>D</b></td><td>재생 속도 한 단계 느리게</td></tr>");
    add("<tr><td><b>F1</b></td><td>도움말</td></tr>");
    add("</table>");
    box.setText(html);
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
    const QString name = QFileInfo(path).fileName();
    view_->clear_frame();  // drops the old frame and mask; the message shows on an empty view
    view_->set_message(tr("%1 로딩중...").arg(name));
    view_->repaint();  // show the message now; opening blocks the UI thread
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = dec_.open(path.toStdString());
    QApplication::restoreOverrideCursor();
    view_->set_message({});
    if (!ok) {
        count_ = 0;
        cur_bgr_.release();
        pix_info_ = {};
        path_.clear();
        timeline_->set_frame_count(0);
        show_result(tr("영상을 열 수 없습니다: %1").arg(path));
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
    loop_view_->hide();
    loop_view_->clear_frame();
    view_->set_blend_frame({});
    loop_cur_ = -1;
    loop_dec_next_ = -1;
    timeline_->set_frame_count(count_);
    update_start_label();
    show_result({});
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
    if (index != dec_next_ && !position(dec_, dec_next_, index)) {
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

void MainWindow::show_loop_frame(int index) {
    if (!loop_) return;
    if (index != loop_dec_next_ && !position(loop_dec_, loop_dec_next_, index)) {
        loop_dec_next_ = -1;
        return;
    }
    const cv::Mat f = loop_dec_.next();
    if (f.empty()) {
        loop_dec_next_ = -1;
        return;
    }
    loop_dec_next_ = index + 1;
    loop_cur_ = index;
    loop_bgr_ = f;
    loop_view_->set_frame(f);
    if (overlay()) view_->set_blend_frame(f);
}

bool MainWindow::overlay() const { return overlay_btn_->isChecked(); }

void MainWindow::set_overlay(bool on) {
    overlay_btn_->setChecked(on);
    apply_overlay();
}

namespace {
struct Tint {
    const char* name;
    QColor color;
};
const Tint kTints[] = {
    {QT_TR_NOOP("없음"), Qt::white},           {QT_TR_NOOP("빨강"), QColor(255, 0, 0)},
    {QT_TR_NOOP("초록"), QColor(0, 255, 0)},   {QT_TR_NOOP("파랑"), QColor(0, 0, 255)},
    {QT_TR_NOOP("시안"), QColor(0, 255, 255)},
};
}  // namespace

void MainWindow::cycle_tint() {
    tint_ = (tint_ + 1) % static_cast<int>(std::size(kTints));
    view_->set_blend_tint(kTints[tint_].color);
    update_tint_text();
}

void MainWindow::update_tint_text() {
    tint_btn_->setText(tr("색상 필터: %1").arg(tr(kTints[tint_].name)));
}

void MainWindow::update_start_label() {
    start_label_->setText(start_ >= 0 ? tr("S: %1").arg(start_) : tr("S: 없음"));
}

void MainWindow::update_loop_caption() {
    const QColor color(255, 170, 0, 230);
    if (!loop_) return loop_view_->set_caption(tr("반복 구간"), color);
    loop_view_->set_caption(tr("반복 구간 [%1, %2] · %3")
                                .arg(loop_->start)
                                .arg(loop_->end)
                                .arg(format_time(loop_->end - loop_->start + 1, dec_.fps())),
                            color);
}

void MainWindow::retranslate_ui() {
    for (const auto& fn : retranslators_) fn();
    show_result({});  // status messages are transient, so they are dropped rather than left in the old language
    view_->set_caption(tr("원본"), QColor(40, 90, 200, 220));
    update_loop_caption();
    update_tint_text();
    update_start_label();
    update_info();
    update_format_ui();
    language_btn_->setText(i18n::language() == i18n::Lang::English ? QStringLiteral("한국어")
                                                                   : QStringLiteral("English"));
}

void MainWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::LanguageChange && view_) retranslate_ui();
    QMainWindow::changeEvent(e);
}

void MainWindow::apply_overlay() {
    const bool on = overlay() && loop_;
    loop_view_->setVisible(loop_.has_value() && !on);
    view_->set_blend_frame(on ? loop_bgr_ : cv::Mat());
    if (on) split_->setSizes({1, 0});
    else if (loop_) split_->setSizes({1, 1});
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
    update_start_label();
    invalidate_loop();
    update_enabled();
}

void MainWindow::invalidate_loop() {
    if (!loop_) return;
    set_playing(false);
    loop_.reset();
    loop_view_->hide();
    loop_view_->clear_frame();
    view_->set_blend_frame({});
    loop_cur_ = -1;
    timeline_->clear_loop();
    view_->set_in_loop(false);
    show_result({});
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
    show_result(tr("탐색 중..."));
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
        show_result(tr("원본 파일을 덮어쓸 수 없습니다"));
        return;
    }

    set_playing(false);
    worker_ = {};  // previous worker has already finished
    exporting_ = true;
    progress_->setValue(0);
    show_result(tr("추출 중..."));
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
    show_result(ok ? tr("프레임 %1 저장 완료: %2").arg(cur_).arg(out)
                        : tr("PNG 저장 실패: %1").arg(out));
    return ok;
}

bool MainWindow::save_frame_native(const QString& path_arg) {
    if (busy() || count_ <= 0 || dec_next_ != cur_ + 1) return false;  // decoder must hold cur_
    const cv::Mat rgb = dec_.precise_rgb();
    if (rgb.empty()) {
        show_result(tr("이 픽셀 포맷은 원본 저장을 지원하지 않습니다: %1")
                             .arg(QString::fromStdString(pix_info_.pix_fmt)));
        return false;
    }
    const bool deep = rgb.depth() == CV_16U;
    QString out = path_arg;
    if (out.isEmpty()) {
        const QFileInfo src(path_);
        const QString def = QDir(src.absolutePath())
                                .filePath(QStringLiteral("%1_frame_%2_%3.png")
                                              .arg(src.completeBaseName())
                                              .arg(cur_)
                                              .arg(deep ? QStringLiteral("16bit")
                                                        : QStringLiteral("hq")));
        out = QFileDialog::getSaveFileName(this, tr("원본 저장 (고품질 PNG)"), def, tr("PNG (*.png)"));
        if (out.isEmpty()) return false;
        if (QFileInfo(out).suffix().isEmpty()) out += QStringLiteral(".png");
    }
    const QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<qsizetype>(rgb.step[0]),
                     deep ? QImage::Format_RGBX64 : QImage::Format_RGB888);
    const bool ok = img.save(out, "PNG", 0);
    show_result(ok ? tr("프레임 %1 원본 저장 완료 (%2bit): %3")
                              .arg(cur_)
                              .arg(deep ? 16 : 8)
                              .arg(out)
                        : tr("원본 저장 실패: %1").arg(out));
    return ok;
}

void MainWindow::show_result(const QString& text) {
    result_->setText(text);
    result_->setToolTip(text);
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
    } else {
        rec = tr("권장: 원본 저장 (정확한 색 변환%1)")
                  .arg(p.bit_depth > 8 ? tr(" + 16bit PNG") : QString());
    }
    format_label_->setText(tr("픽셀 포맷: %1 · %2bit · %3 · %4  →  %5")
                               .arg(QString::fromStdString(p.pix_fmt))
                               .arg(p.bit_depth)
                               .arg(QString::fromStdString(p.color_range),
                                    QString::fromStdString(p.colorspace), rec));
    format_label_->setToolTip(format_label_->text());
    emphasize(save_native_btn_, !p.png_exact);
    emphasize(save_png_btn_, p.png_exact);
    save_png_btn_->setToolTip(p.png_exact
                                  ? tr("원본 픽셀과 동일한 무손실 PNG")
                                  : tr("화면 표시용 빠른 변환(8bit, 기본 색 행렬)을 그대로 저장한 PNG. 마스크 미포함"));
    save_native_btn_->setToolTip(tr("원본 프레임에서 따로 변환한 컬러 PNG: 영상의 색 행렬/범위 반영, 정확한 반올림, "
                                  "크로마 풀 보간, 10bit 이상이면 16bit PNG. 마스크 미포함"));
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
    show_result(msg);
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
        update_start_label();
        progress_->setValue(1000);
        timeline_->set_loop(r.range.start, r.range.end);
        show_result(tr("반복 구간 [%1, %2] · %3프레임 · 길이 %4 · 유사도 %5%")
                             .arg(r.range.start)
                             .arg(r.range.end)
                             .arg(r.range.end - r.range.start + 1)
                             .arg(format_time(r.range.end - r.range.start + 1, dec_.fps()))
                             .arg(r.similarity * 100, 0, 'f', 2));
        show_frame(r.range.start);
        loop_dec_next_ = -1;
        if (loop_dec_.open(path_.toStdString())) {
            update_loop_caption();
            show_loop_frame(r.range.start);
            apply_overlay();  // split view (equal halves) or blended onto the original
        }
        break;
    case LoopStatus::NotFound:
        progress_->setValue(1000);
        show_result(tr(loop_ ? "E 이후 일치 프레임 없음" : "반복 없음"));
        break;
    case LoopStatus::Cancelled:
        show_result(tr("취소됨"));
        break;
    case LoopStatus::Error:
        show_result(tr("오류: 탐색 실패"));
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
    show_loop_frame(cur_);  // cur_ is inside the loop: start both views in phase
    restart_play_clock();
    play_timer_.start(play_interval_ms());
}

double MainWindow::play_rate() const {
    const double fps = dec_.fps() > 0 ? dec_.fps() : 30.0;
    return fps * speed_combo_->currentData().toDouble();
}

int MainWindow::play_interval_ms() const {
    // Never tick faster than the display refreshes; at high speeds tick() skips frames.
    constexpr int kMinTickMs = 15;
    return std::max(kMinTickMs, static_cast<int>(std::lround(1000.0 / play_rate())));
}

void MainWindow::restart_play_clock() {
    play_clock_.restart();
    play_frames_ = 0;
}

void MainWindow::tick() {
    if (!loop_) return set_playing(false);
    // Advance by however many frames are due on the wall clock, so the speed holds
    // even when ticks arrive late or a frame takes longer than its interval to show.
    const auto due = static_cast<qint64>(play_clock_.elapsed() * play_rate() / 1000.0);
    const int step = static_cast<int>(due - play_frames_);
    if (step <= 0) return;
    play_frames_ = due;
    // The original runs on past E (restarting at S after the last frame);
    // the loop view cycles [S, E] on the same clock.
    show_frame(wrap_step(cur_, step, loop_->start, count_ - 1));
    show_loop_frame(wrap_step(loop_cur_, step, loop_->start, loop_->end));
}

void MainWindow::update_info() {
    threshold_label_->setText(QStringLiteral("%1%").arg(threshold() * 100, 0, 'f', 1));
    brush_label_->setText(QStringLiteral("%1px").arg(brush_slider_->value()));
    if (count_ <= 0) {
        info_->setText(tr("영상 없음"));
        return;
    }
    const double fps = dec_.fps();
    const auto fmt = [fps](int frame) { return format_time(frame, fps); };
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
    save_native_btn_->setEnabled(loaded && idle);
    play_btn_->setEnabled(loop_.has_value() && idle);
    overlay_btn_->setEnabled(loop_.has_value());
    overlay_slider_->setEnabled(loop_.has_value());
    tint_btn_->setEnabled(loop_.has_value());
}

}  // namespace dfd
