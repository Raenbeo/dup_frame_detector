// usage: export_test <video> <first> <last> <out.mkv>
// Exports [first, last] losslessly, reopens the output and checks that every
// frame is bit-identical to the source frame.
#include "decoder/decoder.hpp"

#include <opencv2/core.hpp>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc < 5) return 2;
    const int first = std::atoi(argv[2]);
    const int last = std::atoi(argv[3]);

    dfd::Decoder src;
    if (!src.open(argv[1])) { std::puts("open failed"); return 1; }
    const auto res = src.export_lossless(first, last, argv[4]);
    if (res.status != dfd::ExportStatus::Ok) {
        std::printf("export failed: %s\n", res.error.c_str());
        return 1;
    }

    dfd::Decoder out;
    if (!out.open(argv[4])) { std::puts("output open failed"); return 1; }
    if (out.frame_count() != last - first + 1) {
        std::printf("frame count %d != %d\n", out.frame_count(), last - first + 1);
        return 1;
    }
    if (!src.seek(first)) { std::puts("seek failed"); return 1; }
    for (int i = first; i <= last; ++i) {
        const cv::Mat a = src.next(), b = out.next();
        if (a.empty() || b.empty() || a.size() != b.size() || cv::norm(a, b, cv::NORM_INF) != 0) {
            std::printf("frame %d differs\n", i);
            return 1;
        }
    }
    std::printf("OK: %d frames bit-identical\n", last - first + 1);
    return 0;
}
