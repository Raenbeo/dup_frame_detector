#include "matcher/matcher.hpp"

#include "decoder/decoder.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace dfd {

namespace {

double masked_mean(const cv::Mat& m, const cv::Mat& include_mask) {
    return include_mask.empty() ? cv::mean(m)[0] : cv::mean(m, include_mask)[0];
}

double mae_similarity(const cv::Mat& a, const cv::Mat& b, const cv::Mat& include_mask) {
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    return 1.0 - masked_mean(diff, include_mask) / 255.0;
}

// Mean SSIM (Gaussian 11x11, sigma 1.5) over included pixels. Excluded pixels
// of `b` are replaced with `a`'s so excluded content can't leak into nearby
// windows.
double ssim_similarity(const cv::Mat& a, const cv::Mat& b, const cv::Mat& include_mask) {
    cv::Mat bb = b;
    if (!include_mask.empty()) {
        bb = a.clone();
        b.copyTo(bb, include_mask);
    }
    constexpr double c1 = (0.01 * 255) * (0.01 * 255), c2 = (0.03 * 255) * (0.03 * 255);
    cv::Mat x, y;
    a.convertTo(x, CV_32F);
    bb.convertTo(y, CV_32F);
    auto blur = [](const cv::Mat& m) {
        cv::Mat out;
        cv::GaussianBlur(m, out, {11, 11}, 1.5);
        return out;
    };
    const cv::Mat mx = blur(x), my = blur(y);
    const cv::Mat mx2 = mx.mul(mx), my2 = my.mul(my), mxy = mx.mul(my);
    const cv::Mat sx2 = blur(x.mul(x)) - mx2, sy2 = blur(y.mul(y)) - my2,
                  sxy = blur(x.mul(y)) - mxy;
    cv::Mat num = (2 * mxy + c1).mul(2 * sxy + c2);
    cv::Mat den = (mx2 + my2 + c1).mul(sx2 + sy2 + c2);
    cv::Mat map;
    cv::divide(num, den, map);
    return std::clamp(masked_mean(map, include_mask), 0.0, 1.0);
}

// Pearson correlation over included pixels, clamped to [0, 1]. Flat regions
// have no structure to correlate, so fall back to MAE there.
double ncc_similarity(const cv::Mat& a, const cv::Mat& b, const cv::Mat& include_mask) {
    cv::Scalar ma, sa, mb, sb;
    cv::meanStdDev(a, ma, sa, include_mask);
    cv::meanStdDev(b, mb, sb, include_mask);
    constexpr double flat = 1e-3;
    if (sa[0] < flat && sb[0] < flat) return mae_similarity(a, b, include_mask);
    if (sa[0] < flat || sb[0] < flat) return 0.0;
    cv::Mat x, y;
    a.convertTo(x, CV_32F, 1.0, -ma[0]);
    b.convertTo(y, CV_32F, 1.0, -mb[0]);
    const double cov = masked_mean(x.mul(y), include_mask);
    return std::clamp(cov / (sa[0] * sb[0]), 0.0, 1.0);
}

}  // namespace

double default_threshold(Metric m) {
    switch (m) {
    case Metric::Mae: return 0.995;
    case Metric::Ssim: return 0.97;
    case Metric::Ncc: return 0.99;
    }
    return 0.995;
}

double similarity(const cv::Mat& a, const cv::Mat& b, const cv::Mat& include_mask,
                  Metric metric) {
    CV_Assert(a.type() == CV_8UC1 && a.size() == b.size() && a.type() == b.type());
    switch (metric) {
    case Metric::Mae: return mae_similarity(a, b, include_mask);
    case Metric::Ssim: return ssim_similarity(a, b, include_mask);
    case Metric::Ncc: return ncc_similarity(a, b, include_mask);
    }
    return 0.0;
}

namespace {

cv::Mat to_gray(const cv::Mat& m) {
    if (m.channels() == 1) return m;
    cv::Mat g;
    cv::cvtColor(m, g, m.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    return g;
}

cv::Mat downscale(const cv::Mat& gray, cv::Size size) {
    cv::Mat out;
    cv::resize(gray, out, size, 0, 0, cv::INTER_AREA);
    return out;
}

// Keep only coarse pixels whose whole source block is included, so excluded
// content never leaks into the coarse comparison.
cv::Mat downscale_mask(const cv::Mat& mask, cv::Size size) {
    if (mask.empty()) return {};
    cv::Mat bin = mask != 0, area;
    cv::resize(bin, area, size, 0, 0, cv::INTER_AREA);
    cv::Mat strict = area == 255;
    if (cv::countNonZero(strict) > 0) return strict;
    cv::Mat nearest;  // mask too thin for strict blocks: fall back to nearest
    cv::resize(bin, nearest, size, 0, 0, cv::INTER_NEAREST);
    return nearest;
}

}  // namespace

LoopResult find_loop(const FrameSource& next, int start, int frame_count,
                     const cv::Mat& include_mask, const LoopOptions& opts) {
    LoopResult res;
    if (start < 0 || start >= frame_count) return res;

    const cv::Mat s = to_gray(next());
    if (s.empty()) return res;
    if (!include_mask.empty() &&
        (include_mask.size() != s.size() || include_mask.type() != CV_8UC1))
        return res;

    const bool coarse = opts.coarse_div > 1 && s.cols >= opts.coarse_div &&
                        s.rows >= opts.coarse_div;
    cv::Size csize;
    cv::Mat s_small, mask_small;
    if (coarse) {
        csize = {s.cols / opts.coarse_div, s.rows / opts.coarse_div};
        s_small = downscale(s, csize);
        mask_small = downscale_mask(include_mask, csize);
    }
    // MAE: area averaging cannot increase MAE (|mean(a-b)| <= mean|a-b|), so the
    // coarse similarity is >= the full one; allow 1 level of rounding slack.
    // SSIM/NCC: no such bound, but downscaling averages out noise and usually
    // raises them; use a wider margin so the coarse pass stays a loose filter.
    const double coarse_threshold =
        opts.threshold - (opts.metric == Metric::Mae ? 1.0 / 255.0 : 0.05);

    // Adjacent frames of slow/static footage match S immediately. Only accept a
    // match after at least one frame has departed from S (similarity < threshold).
    bool departed = false;
    const int total = frame_count - 1 - start;
    for (int i = start + 1, done = 1; i < frame_count; ++i, ++done) {
        if (opts.stop.stop_requested()) {
            res.status = LoopStatus::Cancelled;
            return res;
        }
        cv::Mat f = next();
        if (f.empty()) break;  // stream ended early
        f = to_gray(f);
        if (f.size() != s.size()) return res;

        const bool candidate =
            !coarse || similarity(s_small, downscale(f, csize), mask_small, opts.metric) >=
                           coarse_threshold;
        if (!candidate) {
            departed = true;  // coarse sim (upper bound / loose filter) rules out a match
        } else {
            const double sim = similarity(s, f, include_mask, opts.metric);
            if (sim < opts.threshold) {
                departed = true;
            } else if (departed) {
                res.status = LoopStatus::Found;
                res.range = {start, i - 1};
                res.similarity = sim;
                if (opts.progress) opts.progress(total, total);
                return res;
            }
        }
        if (opts.progress) opts.progress(done, total);
    }
    res.status = LoopStatus::NotFound;
    return res;
}

LoopResult find_loop(Decoder& dec, int start, const cv::Mat& include_mask,
                     const LoopOptions& opts) {
    if (!dec.seek(start)) return {};
    return find_loop([&dec] { return dec.next(); }, start, dec.frame_count(), include_mask,
                     opts);
}

LoopResult find_next_loop(Decoder& dec, const LoopRange& prev, const cv::Mat& include_mask,
                          const LoopOptions& opts) {
    LoopResult res;
    if (!prev.found() || !dec.seek(prev.start)) return res;
    const int count = dec.frame_count();
    const int len = prev.end - prev.start + 1;

    const cv::Mat s = to_gray(dec.next());
    if (s.empty()) return res;
    if (!include_mask.empty() &&
        (include_mask.size() != s.size() || include_mask.type() != CV_8UC1))
        return res;

    const bool coarse = opts.coarse_div > 1 && s.cols >= opts.coarse_div &&
                        s.rows >= opts.coarse_div;
    cv::Size csize;
    cv::Mat mask_small;
    if (coarse) {
        csize = {s.cols / opts.coarse_div, s.rows / opts.coarse_div};
        mask_small = downscale_mask(include_mask, csize);
    }
    const double coarse_threshold =
        opts.threshold - (opts.metric == Metric::Mae ? 1.0 / 255.0 : 0.05);
    const auto small = [&](const cv::Mat& g) { return coarse ? downscale(g, csize) : g; };
    const cv::Mat& cmask = coarse ? mask_small : include_mask;
    const double cthr = coarse ? coarse_threshold : opts.threshold;

    // One cycle of the current loop; later frames are compared with the same phase.
    std::vector<cv::Mat> cycle;
    cycle.reserve(len);
    const cv::Mat s_small = small(s);
    cycle.push_back(s_small);

    const int total = count - 1 - prev.start;
    bool broke = false;  // the repetition run has ended
    int s2 = -1;
    cv::Mat s2_frame;
    for (int i = prev.start + 1, done = 1; i < count; ++i, ++done) {
        if (opts.stop.stop_requested()) {
            res.status = LoopStatus::Cancelled;
            return res;
        }
        cv::Mat f = dec.next();
        if (f.empty()) break;
        f = to_gray(f);
        if (f.size() != s.size()) return res;
        const cv::Mat fs = small(f);
        if (i <= prev.end) {
            cycle.push_back(fs);
        } else {
            if (!broke &&
                similarity(cycle[(i - prev.start) % len], fs, cmask, opts.metric) < cthr)
                broke = true;
            if (broke && similarity(s_small, fs, cmask, opts.metric) >= cthr &&
                similarity(s, f, include_mask, opts.metric) >= opts.threshold) {
                s2 = i;
                s2_frame = f;
                break;
            }
        }
        if (opts.progress) opts.progress(done, total);
    }
    if (s2 < 0) {
        res.status = LoopStatus::NotFound;
        return res;
    }

    // Continue the same sequential read: yield S2 first, then the following frames.
    bool first = true;
    LoopOptions o = opts;
    const int off = s2 - prev.start;
    if (opts.progress)
        o.progress = [&opts, off](int d, int t) { opts.progress(off + d, off + t); };
    return find_loop(
        [&] {
            if (first) {
                first = false;
                return s2_frame;
            }
            return dec.next();
        },
        s2, count, include_mask, o);
}

}  // namespace dfd
