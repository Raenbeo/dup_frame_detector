// usage: loop_test <video> <start> <expected_end> [threshold] [mae|ssim|ncc]
// Runs find_loop on a real video and checks the reported [S, E-1].
#include "decoder/decoder.hpp"
#include "matcher/matcher.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 4) return 2;
    dfd::Decoder dec;
    if (!dec.open(argv[1])) { std::puts("open failed"); return 1; }
    const int start = std::atoi(argv[2]), expected_end = std::atoi(argv[3]);
    dfd::LoopOptions opts;
    if (argc > 5) {
        if (!std::strcmp(argv[5], "ssim")) opts.metric = dfd::Metric::Ssim;
        else if (!std::strcmp(argv[5], "ncc")) opts.metric = dfd::Metric::Ncc;
        opts.threshold = dfd::default_threshold(opts.metric);
    }
    if (argc > 4 && std::atof(argv[4]) > 0) opts.threshold = std::atof(argv[4]);

    const auto t0 = std::chrono::steady_clock::now();
    const auto r = dfd::find_loop(dec, start, {}, opts);
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0);

    std::printf("status=%d range=[%d, %d] sim=%.4f (%.1f ms)\n", static_cast<int>(r.status),
                r.range.start, r.range.end, r.similarity, ms.count());
    const bool ok = expected_end < 0 ? r.status == dfd::LoopStatus::NotFound
                                     : r.status == dfd::LoopStatus::Found &&
                                           r.range.start == start && r.range.end == expected_end;
    std::puts(ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
