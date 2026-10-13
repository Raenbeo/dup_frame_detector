#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace dfd {

struct ExportOptions {
    // Called after each written frame with (frames done, frames total).
    std::function<void(int, int)> progress;
    std::stop_token stop;
};

enum class ExportStatus { Ok, Cancelled, Error };

struct ExportResult {
    ExportStatus status = ExportStatus::Error;
    std::string error;  // set when status == Error
};

// Native (pre-conversion) pixel layout of the last decoded frame.
struct PixelInfo {
    bool valid = false;
    std::string pix_fmt;     // FFmpeg name, e.g. "yuv420p"
    int bit_depth = 0;
    std::string color_range;  // "tv" / "pc" / "unknown"
    std::string colorspace;   // e.g. "bt709"
    bool rgb = false;
    // 8-bit RGB without alpha: the BGR Mat / PNG holds exactly the native values.
    bool png_exact = false;
};

// FFmpeg wrapper. Frame-accurate access; returns BGR cv::Mat.
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const std::string& path);
    void close();

    int frame_count() const;
    double fps() const;
    cv::Size size() const;

    // Position the decoder so the next call to next() returns `index`.
    bool seek(int index);
    // Layout of the frame most recently returned by next() (cheap, no copy).
    PixelInfo pixel_info() const;
    // That frame converted to RGB as faithfully as possible, independent of the
    // fast display path: the stream's colour matrix and range, accurate rounding,
    // full chroma interpolation. CV_8UC3 (R,G,B) for <= 8-bit sources, otherwise
    // CV_16UC4 (R,G,B,X with X = 0xFFFF). Empty on failure.
    cv::Mat precise_rgb() const;

    // Decode the next frame. Empty Mat on end of stream / error.
    cv::Mat next();
    // Decode and discard the next n frames without converting them (cheaper than
    // next()). false on end of stream / error.
    bool skip(int n);

    // Writes frames [first, last] to `path` (Matroska) as FFV1: mathematically
    // lossless, native pixel format (no RGB round trip), source timestamps and
    // colour metadata kept. Decoded frames of the output equal the source's.
    // Moves the decoder position; seek() before using it again.
    ExportResult export_lossless(int first, int last, const std::string& path,
                                 const ExportOptions& opts = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dfd
