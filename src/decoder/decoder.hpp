#pragma once

#include <opencv2/core.hpp>

#include <functional>
#include <memory>
#include <stop_token>
#include <string>

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
