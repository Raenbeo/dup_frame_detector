#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <stop_token>

namespace dfd {

class Decoder;

// How two frames are compared. All map to a similarity in [0, 1].
enum class Metric {
    Mae,   // 1 - MAE/255: exact pixel values; strict, fast
    Ssim,  // structural similarity: tolerant of compression noise / blur
    Ncc,   // zero-mean normalized cross-correlation: tolerant of brightness/contrast shifts
};

// Suggested threshold for each metric (scales differ between metrics).
double default_threshold(Metric m);

// Similarity of `a` and `b` over masked pixels, in [0, 1].
// `a`, `b`: 8-bit single-channel (grayscale), same size.
// `include_mask`: 8-bit single-channel, nonzero = compare, 0 = excluded.
//                 Empty = compare all pixels.
double similarity(const cv::Mat& a, const cv::Mat& b, const cv::Mat& include_mask,
                  Metric metric = Metric::Mae);

struct LoopRange {
    int start = -1;  // S
    int end = -1;    // E - 1 (inclusive last frame of the loop)
    bool found() const { return start >= 0 && end >= start; }
};

enum class LoopStatus { Found, NotFound, Cancelled, Error };

struct LoopResult {
    LoopStatus status = LoopStatus::Error;
    LoopRange range;          // valid when status == Found
    double similarity = 0.0;  // full-resolution similarity of S vs E (when Found)
};

struct LoopOptions {
    Metric metric = Metric::Mae;
    double threshold = 0.995;  // match when similarity >= threshold, in [0, 1]
    int coarse_div = 4;       // coarse pass at 1/coarse_div resolution; <= 1 disables it
    // Called after each compared frame with (frames done, frames total).
    std::function<void(int, int)> progress;
    std::stop_token stop;
};

// Returns the next frame (BGR or gray, 8-bit) in sequence; empty Mat at end.
using FrameSource = std::function<cv::Mat()>;

// `next` must yield frame S first, then S+1, S+2, ... up to `frame_count - 1`.
// Compares S against each later frame i; stops at the first E with
// similarity(S, E) >= threshold *after* some earlier frame fell below the
// threshold (so near-identical neighbours of S don't end the search), and
// reports the loop [S, E-1].
LoopResult find_loop(const FrameSource& next, int start, int frame_count,
                     const cv::Mat& include_mask, const LoopOptions& opts);

// Convenience overload: seeks `dec` to `start` and reads sequentially.
LoopResult find_loop(Decoder& dec, int start, const cv::Mat& include_mask,
                     const LoopOptions& opts);

}  // namespace dfd
