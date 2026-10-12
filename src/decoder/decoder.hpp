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
    // Every component sits in its own plane (YUV/GBR/gray, 8..16 bit, little endian),
    // so the planes can be written out untouched.
    bool planar_saveable = false;
};

struct NativeFrame {
    PixelInfo info;
    std::vector<cv::Mat> planes;  // CV_8UC1 or CV_16UC1, native plane sizes
    std::vector<std::string> plane_names;  // "Y","U","V" / "G","B","R" (+ "A")
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
    // Untouched planes of that frame; empty planes unless info.planar_saveable.
    NativeFrame native_frame() const;

    // Decode the next frame. Empty Mat on end of stream / error.
    cv::Mat next();

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
