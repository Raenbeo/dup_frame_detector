// usage: ui_smoke_test <loop_video> [screenshot.png]
// loop_video: 90 frames = a 30-frame clip repeated 3 times (see loop_test).
// Run with QT_QPA_PLATFORM=offscreen.
#include "ui/main_window.hpp"
#include "ui/timeline.hpp"
#include "ui/video_view.hpp"

#include <QApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <QTimer>

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) {                                      \
            std::printf("line %d: %s\n", __LINE__, #cond); \
            ++failures;                                     \
        }                                                   \
    } while (0)

static dfd::LoopStatus search(dfd::MainWindow& w) {
    dfd::LoopStatus st = dfd::LoopStatus::Error;
    QEventLoop loop;
    QObject::connect(&w, &dfd::MainWindow::searchFinished, &loop, [&](dfd::LoopStatus s) {
        st = s;
        loop.quit();
    });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    w.start_search();
    loop.exec();
    return st;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc < 2) return 2;
    dfd::MainWindow w;
    w.show();
    CHECK(w.open_video(argv[1]));
    CHECK(!w.open_video("/nonexistent.mp4"));
    CHECK(w.open_video(argv[1]));

    dfd::VideoView* v = w.view();
    CHECK(v->video_size() == cv::Size(640, 480));
    CHECK(w.current_frame() == 0);

    // scrub + S
    w.timeline()->frameRequested(10);
    CHECK(w.current_frame() == 10);
    w.set_start_from_current();
    CHECK(w.start_frame() == 10);

    // search with default threshold
    CHECK(search(w) == dfd::LoopStatus::Found);
    CHECK(w.loop() && w.loop()->start == 10 && w.loop()->end == 39);
    CHECK(w.current_frame() == 10);

    // lossless export of the found loop
    {
        const QString out = QDir::temp().filePath("dfd_ui_export_test.mkv");
        QFile::remove(out);
        bool ok = false;
        QEventLoop el;
        QObject::connect(&w, &dfd::MainWindow::exportFinished, &el, [&](bool o, QString) {
            ok = o;
            el.quit();
        });
        QTimer::singleShot(20000, &el, &QEventLoop::quit);
        w.export_loop(out);
        CHECK(w.exporting());
        el.exec();
        CHECK(ok && !w.exporting() && QFileInfo(out).size() > 0 && w.loop());
        dfd::Decoder d;
        CHECK(d.open(out.toStdString()) && d.frame_count() == 30);
        QFile::remove(out);
    }

    // loop playback wraps inside [10, 39]
    w.set_playing(true);
    CHECK(w.playing());
    int max_seen = 0, min_seen = 1 << 30;
    bool wrapped = false;
    QElapsedTimer t;
    t.start();
    int prev = w.current_frame();
    while (t.elapsed() < 1800) {
        QTest::qWait(10);
        const int c = w.current_frame();
        max_seen = std::max(max_seen, c);
        min_seen = std::min(min_seen, c);
        if (c < prev) wrapped = true;
        prev = c;
    }
    CHECK(min_seen >= 10 && max_seen <= 39 && wrapped);
    w.timeline()->frameRequested(50);  // user scrub stops playback
    CHECK(!w.playing() && w.current_frame() == 50);

    // mask painting: mouse drag on the view
    CHECK(!v->has_mask());
    const QPoint a = v->widget_pos({100, 100}).toPoint();
    const QPoint b = v->widget_pos({300, 100}).toPoint();
    QTest::mousePress(v, Qt::LeftButton, {}, a);
    QTest::mouseMove(v, b);
    QTest::mouseRelease(v, Qt::LeftButton, {}, b);
    CHECK(v->has_mask());
    CHECK(!w.loop());  // mask edit invalidates the result
    cv::Mat inc = v->include_mask();
    CHECK(!inc.empty() && inc.at<uchar>(100, 200) == 0 && inc.at<uchar>(400, 600) == 255);
    // eraser removes it
    v->set_eraser(true);
    v->set_brush_radius(60);
    QTest::mousePress(v, Qt::LeftButton, {}, a);
    QTest::mouseMove(v, b);
    QTest::mouseRelease(v, Qt::LeftButton, {}, b);
    CHECK(!v->has_mask());
    v->set_eraser(false);

    // threshold too strict -> no repeat; cancel path
    w.timeline()->frameRequested(75);
    w.set_start_from_current();
    CHECK(search(w) == dfd::LoopStatus::NotFound);
    CHECK(!w.loop());

    // cancel: start search and cancel immediately
    w.timeline()->frameRequested(0);
    w.set_start_from_current();
    w.set_threshold(1.0);
    {
        dfd::LoopStatus st = dfd::LoopStatus::Error;
        QEventLoop loop;
        QObject::connect(&w, &dfd::MainWindow::searchFinished, &loop, [&](dfd::LoopStatus s) {
            st = s;
            loop.quit();
        });
        w.start_search();
        w.cancel_search();
        loop.exec();
        CHECK(st == dfd::LoopStatus::Cancelled || st == dfd::LoopStatus::NotFound);
    }

    if (argc > 2) {
        w.set_threshold(0.995);
        w.timeline()->frameRequested(10);
        w.set_start_from_current();
        v->set_brush_radius(25);
        QTest::mousePress(v, Qt::LeftButton, {}, a);
        QTest::mouseMove(v, b);
        QTest::mouseRelease(v, Qt::LeftButton, {}, b);
        w.view()->clear_mask();
        search(w);
        w.grab().save(argv[2]);
    }

    std::puts(failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
