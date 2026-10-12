#include "matcher/matcher.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <stop_token>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond)) {                                      \
            std::printf("line %d: %s\n", __LINE__, #cond); \
            ++failures;                                     \
        }                                                   \
    } while (0)
#define CHECK_NEAR(a, b)                                                          \
    do {                                                                          \
        const double va = (a), vb = (b);                                          \
        if (std::abs(va - vb) > 1e-9) {                                           \
            std::printf("line %d: %s = %f, expected %f\n", __LINE__, #a, va, vb); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

namespace {

cv::Mat noise(int seed) {
    cv::Mat m(48, 64, CV_8UC3);
    cv::RNG(seed).fill(m, cv::RNG::UNIFORM, 0, 256);
    return m;
}

dfd::FrameSource from(const std::vector<cv::Mat>& frames, int start) {
    return [&frames, i = start]() mutable {
        return i < static_cast<int>(frames.size()) ? frames[i++] : cv::Mat{};
    };
}

dfd::LoopResult run(const std::vector<cv::Mat>& frames, int start, const cv::Mat& mask = {},
                    dfd::LoopOptions opts = {}) {
    return dfd::find_loop(from(frames, start), start, static_cast<int>(frames.size()), mask,
                          opts);
}

void test_find_loop() {
    using dfd::LoopStatus;
    // period-5 sequence of distinct noise frames
    std::vector<cv::Mat> loop;
    for (int k = 0; k < 17; ++k) loop.push_back(noise(k % 5));

    for (int div : {1, 4}) {
        dfd::LoopOptions o;
        o.coarse_div = div;
        auto r = run(loop, 0, {}, o);
        CHECK(r.status == LoopStatus::Found && r.range.start == 0 && r.range.end == 4);
        CHECK_NEAR(r.similarity, 1.0);
        r = run(loop, 2, {}, o);
        CHECK(r.status == LoopStatus::Found && r.range.start == 2 && r.range.end == 6);
    }
    // next match after E: S=0, E=5 matched; searching from E+1=6 finds E2=10 -> [0, 9]
    {
        bool first = true;
        auto rest = from(loop, 6);
        auto r = dfd::find_loop(
            [&] {
                if (first) {
                    first = false;
                    return loop[0];
                }
                return rest();
            },
            0, static_cast<int>(loop.size()), {}, {}, 6);
        CHECK(r.status == LoopStatus::Found && r.range.start == 0 && r.range.end == 9);
    }
    // last frame has no successor -> not found; out of range -> error
    CHECK(run(loop, 16).status == LoopStatus::NotFound);
    CHECK(run(loop, 17).status == LoopStatus::Error);
    CHECK(run(loop, -1).status == LoopStatus::Error);

    // no repeat
    std::vector<cv::Mat> uniq;
    for (int k = 0; k < 10; ++k) uniq.push_back(noise(100 + k));
    CHECK(run(uniq, 0).status == LoopStatus::NotFound);

    // threshold: E differs from S by +10 everywhere -> similarity ~ 1 - 10/255
    std::vector<cv::Mat> near = {cv::Mat(48, 64, CV_8UC3, cv::Scalar(100, 100, 100)),
                                 noise(1), noise(2),
                                 cv::Mat(48, 64, CV_8UC3, cv::Scalar(110, 110, 110))};
    for (int div : {1, 4}) {
        dfd::LoopOptions o;
        o.coarse_div = div;
        o.threshold = 0.97;
        CHECK(run(near, 0, {}, o).status == LoopStatus::NotFound);
        o.threshold = 0.95;
        auto r = run(near, 0, {}, o);
        CHECK(r.status == LoopStatus::Found && r.range.end == 2);
    }

    // mask: frames identical except a changing counter box in the top-left
    std::vector<cv::Mat> counter;
    const cv::Mat base = noise(7);
    for (int k = 0; k < 6; ++k) {
        cv::Mat f = base.clone();
        f(cv::Rect(0, 0, 16, 16)).setTo(cv::Scalar::all(k * 40));
        counter.push_back(f);
    }
    // S vs S+1 differ in 64 of 3072 px by 40 -> ~0.9967 without mask
    dfd::LoopOptions strict;
    strict.threshold = 1.0;
    CHECK(run(counter, 0, {}, strict).status == LoopStatus::NotFound);
    cv::Mat include(48, 64, CV_8UC1, cv::Scalar(255));
    include(cv::Rect(0, 0, 16, 16)).setTo(cv::Scalar(0));
    for (int div : {1, 4}) {
        strict.coarse_div = div;
        auto r = run(counter, 0, include, strict);
        // mask hides the only difference: S+1 matches but never departed -> no loop
        CHECK(r.status == LoopStatus::NotFound);
    }
    // mask size mismatch -> error
    CHECK(run(counter, 0, cv::Mat(10, 10, CV_8UC1, cv::Scalar(255))).status ==
          LoopStatus::Error);

    // progress + cancel
    {
        std::stop_source src;
        dfd::LoopOptions o;
        o.stop = src.get_token();
        std::vector<int> seen;
        o.progress = [&](int done, int total) {
            seen.push_back(done);
            CHECK(total == 9);
            if (done == 3) src.request_stop();
        };
        CHECK(run(uniq, 0, {}, o).status == LoopStatus::Cancelled);
        CHECK((seen == std::vector<int>{1, 2, 3}));
    }
    {
        dfd::LoopOptions o;
        int last = 0, calls = 0;
        o.progress = [&](int done, int total) { last = done, ++calls; CHECK(total == 9); };
        CHECK(run(uniq, 0, {}, o).status == LoopStatus::NotFound);
        CHECK(last == 9 && calls == 9);
        o.progress = [&](int done, int total) { last = done; CHECK(total == 16); };
        CHECK(run(loop, 0, {}, o).status == LoopStatus::Found);
        CHECK(last == 16);  // completes to 100% on match
    }
}

void test_metrics() {
    using dfd::Metric;
    using dfd::similarity;
    cv::Mat g = noise(3), a, b;
    cv::cvtColor(g, a, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(a, a, {5, 5}, 0);  // smooth content, like real video

    for (Metric m : {Metric::Mae, Metric::Ssim, Metric::Ncc}) {
        CHECK(similarity(a, a.clone(), {}, m) > 1.0 - 1e-5);  // float math for SSIM/NCC
    }

    // brightness/contrast shift: NCC ignores it, MAE doesn't
    a.convertTo(b, -1, 0.9, 40);
    CHECK(similarity(a, b, {}, Metric::Ncc) > 0.999);
    CHECK(similarity(a, b, {}, Metric::Mae) < 0.95);

    // mild noise: SSIM stays higher than on unrelated content
    cv::Mat n(a.size(), CV_8UC1), noisy;
    cv::RNG(5).fill(n, cv::RNG::NORMAL, 0, 3);
    cv::add(a, n, noisy, cv::noArray(), CV_8U);
    cv::Mat other;
    cv::cvtColor(noise(4), other, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(other, other, {5, 5}, 0);
    for (Metric m : {Metric::Ssim, Metric::Ncc})
        CHECK(similarity(a, noisy, {}, m) > similarity(a, other, {}, m));

    // mask hides a differing block for every metric
    cv::Mat c = a.clone();
    c(cv::Rect(0, 0, 16, 16)).setTo(cv::Scalar(255));
    cv::Mat include(a.size(), CV_8UC1, cv::Scalar(255));
    include(cv::Rect(0, 0, 16, 16)).setTo(cv::Scalar(0));
    for (Metric m : {Metric::Mae, Metric::Ssim, Metric::Ncc}) {
        CHECK(similarity(a, c, {}, m) < 1.0 - 1e-6);
        CHECK(similarity(a, c, include, m) > 1.0 - 1e-5);
    }

    // flat frames under NCC fall back to MAE
    CHECK_NEAR(similarity(cv::Mat(8, 8, CV_8UC1, cv::Scalar(100)),
                          cv::Mat(8, 8, CV_8UC1, cv::Scalar(151)), {}, Metric::Ncc),
               0.8);

    // find_loop works with every metric
    std::vector<cv::Mat> loop;
    for (int k = 0; k < 17; ++k) loop.push_back(noise(k % 5));
    for (Metric m : {Metric::Mae, Metric::Ssim, Metric::Ncc})
        for (int div : {1, 4}) {
            dfd::LoopOptions o;
            o.metric = m;
            o.threshold = dfd::default_threshold(m);
            o.coarse_div = div;
            auto r = run(loop, 2, {}, o);
            CHECK(r.status == dfd::LoopStatus::Found && r.range.start == 2 && r.range.end == 6);
        }
}

}  // namespace

int main() {
    using dfd::similarity;
    const cv::Mat a(8, 8, CV_8UC1, cv::Scalar(100));

    // identical -> 1.0, with and without mask
    CHECK_NEAR(similarity(a, a.clone(), {}), 1.0);
    CHECK_NEAR(similarity(a, a.clone(), cv::Mat(8, 8, CV_8UC1, cv::Scalar(255))), 1.0);

    // uniform difference of 51 -> 1 - 51/255 = 0.8
    CHECK_NEAR(similarity(a, cv::Mat(8, 8, CV_8UC1, cv::Scalar(151)), {}), 0.8);
    // symmetric
    CHECK_NEAR(similarity(cv::Mat(8, 8, CV_8UC1, cv::Scalar(151)), a, {}), 0.8);
    // max difference -> 0.0
    CHECK_NEAR(similarity(cv::Mat(8, 8, CV_8UC1, cv::Scalar(0)),
                          cv::Mat(8, 8, CV_8UC1, cv::Scalar(255)), {}),
               0.0);

    // mask effect: left half differs by 155, right half identical
    cv::Mat c = a.clone();
    c(cv::Rect(0, 0, 4, 8)).setTo(cv::Scalar(255));  // differs by 155 on left half
    CHECK_NEAR(similarity(a, c, {}), 1.0 - (155.0 / 2) / 255.0);

    cv::Mat right_only(8, 8, CV_8UC1, cv::Scalar(0));
    right_only(cv::Rect(4, 0, 4, 8)).setTo(cv::Scalar(255));  // compare right half only
    CHECK_NEAR(similarity(a, c, right_only), 1.0);             // differing area excluded
    cv::Mat left_only(8, 8, CV_8UC1, cv::Scalar(0));
    left_only(cv::Rect(0, 0, 4, 8)).setTo(cv::Scalar(255));
    CHECK_NEAR(similarity(a, c, left_only), 1.0 - 155.0 / 255.0);

    test_find_loop();
    test_metrics();

    std::puts(failures ? "FAIL" : "OK");
    return failures ? 1 : 0;
}
