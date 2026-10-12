// usage: decoder_test <video>
// Checks that seek(N) + next() matches the N-th frame of a sequential decode.
#include "decoder/decoder.hpp"

#include <opencv2/core.hpp>

#include <cstdio>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    dfd::Decoder seq;
    if (!seq.open(argv[1])) { std::puts("open failed"); return 1; }
    std::printf("frames=%d fps=%.3f size=%dx%d\n", seq.frame_count(), seq.fps(),
                seq.size().width, seq.size().height);

    std::vector<cv::Mat> all;
    for (cv::Mat f; !(f = seq.next()).empty();) all.push_back(f);
    if (static_cast<int>(all.size()) != seq.frame_count()) {
        std::printf("sequential count %zu != frame_count %d\n", all.size(), seq.frame_count());
        return 1;
    }

    dfd::Decoder d;
    d.open(argv[1]);
    const int n = d.frame_count();
    for (int idx : {n - 1, 0, n / 2, 7, n - 2, 7, 1}) {
        if (!d.seek(idx)) { std::printf("seek(%d) failed\n", idx); return 1; }
        cv::Mat f = d.next();
        if (f.empty() || cv::norm(f, all[idx], cv::NORM_INF) != 0) {
            std::printf("seek(%d) mismatch\n", idx);
            return 1;
        }
    }
    if (d.seek(n) || d.seek(-1)) { std::puts("out-of-range seek accepted"); return 1; }
    std::puts("OK");
    return 0;
}
