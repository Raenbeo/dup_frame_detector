#include "decoder/decoder.hpp"

#include <opencv2/core.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

namespace dfd {

struct Decoder::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* codec = nullptr;
    SwsContext* sws = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    int stream = -1;
    double fps = 0.0;
    cv::Size size;
    // Sorted presentation timestamps of every frame; index in this table
    // is the frame-accurate frame number.
    std::vector<int64_t> pts;
    bool pending = false;  // `frame` holds a decoded frame not yet returned
    bool draining = false;

    void reset() {
        if (sws) sws_freeContext(sws);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (codec) avcodec_free_context(&codec);
        if (fmt) avformat_close_input(&fmt);
        sws = nullptr;
        stream = -1;
        fps = 0.0;
        size = {};
        pts.clear();
        pending = draining = false;
    }

    bool build_index() {
        while (av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == stream) {
                const int64_t t = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
                if (t != AV_NOPTS_VALUE) pts.push_back(t);
            }
            av_packet_unref(pkt);
        }
        std::ranges::sort(pts);
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        return !pts.empty();
    }

    // Decode the next frame into `frame`. false on end of stream / error.
    bool decode() {
        for (;;) {
            const int r = avcodec_receive_frame(codec, frame);
            if (r == 0) return true;
            if (r != AVERROR(EAGAIN)) return false;  // EOF or error
            if (draining) return false;
            for (;;) {
                const int rr = av_read_frame(fmt, pkt);
                if (rr < 0) {
                    draining = true;
                    avcodec_send_packet(codec, nullptr);
                    break;
                }
                const bool ours = pkt->stream_index == stream;
                if (ours) avcodec_send_packet(codec, pkt);
                av_packet_unref(pkt);
                if (ours) break;
            }
        }
    }

    cv::Mat to_bgr() {
        sws = sws_getCachedContext(sws, frame->width, frame->height,
                                   static_cast<AVPixelFormat>(frame->format),
                                   frame->width, frame->height, AV_PIX_FMT_BGR24,
                                   SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!sws) return {};
        cv::Mat out(frame->height, frame->width, CV_8UC3);
        uint8_t* dst[1] = {out.data};
        const int dst_stride[1] = {static_cast<int>(out.step[0])};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dst_stride);
        return out;
    }

    const AVPixFmtDescriptor* desc() const {
        if (!frame || frame->format < 0 || frame->width <= 0) return nullptr;
        return av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    }

    int64_t frame_pts() const {
        return frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp
                                                              : frame->pts;
    }
};

Decoder::Decoder() : impl_(std::make_unique<Impl>()) {}
Decoder::~Decoder() { close(); }

bool Decoder::open(const std::string& path) {
    close();
    auto& d = *impl_;
    if (avformat_open_input(&d.fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(d.fmt, nullptr) < 0) return close(), false;

    const AVCodec* dec = nullptr;
    d.stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (d.stream < 0 || !dec) return close(), false;

    AVStream* st = d.fmt->streams[d.stream];
    d.codec = avcodec_alloc_context3(dec);
    if (!d.codec || avcodec_parameters_to_context(d.codec, st->codecpar) < 0)
        return close(), false;
    d.codec->thread_count = 0;  // auto
    if (avcodec_open2(d.codec, dec, nullptr) < 0) return close(), false;

    d.pkt = av_packet_alloc();
    d.frame = av_frame_alloc();
    d.size = {d.codec->width, d.codec->height};
    const AVRational r = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    d.fps = r.den ? av_q2d(r) : 0.0;

    if (!d.build_index() || !seek(0)) return close(), false;
    return true;
}

void Decoder::close() { impl_->reset(); }

int Decoder::frame_count() const { return static_cast<int>(impl_->pts.size()); }
double Decoder::fps() const { return impl_->fps; }
cv::Size Decoder::size() const { return impl_->size; }

bool Decoder::seek(int index) {
    auto& d = *impl_;
    if (!d.fmt || index < 0 || index >= frame_count()) return false;
    const int64_t target = d.pts[index];

    if (av_seek_frame(d.fmt, d.stream, target, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(d.codec);
    d.pending = d.draining = false;

    // Decode forward from the preceding keyframe until the target frame.
    while (d.decode()) {
        if (d.frame_pts() >= target) {
            d.pending = true;
            return d.frame_pts() == target;
        }
    }
    return false;
}

PixelInfo Decoder::pixel_info() const {
    const auto& d = *impl_;
    PixelInfo p;
    const AVPixFmtDescriptor* desc = d.desc();
    if (!desc) return p;
    const auto str = [](const char* s) { return std::string(s ? s : "unknown"); };
    p.valid = true;
    p.pix_fmt = desc->name;
    p.bit_depth = desc->comp[0].depth;
    p.color_range = str(av_color_range_name(d.frame->color_range));
    p.colorspace = str(av_color_space_name(d.frame->colorspace));
    p.rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;

    const uint64_t bad = AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_HWACCEL;
    p.png_exact = p.rgb && p.bit_depth == 8 && !(desc->flags & (bad | AV_PIX_FMT_FLAG_ALPHA));
    return p;
}

cv::Mat Decoder::precise_rgb() const {
    const auto& d = *impl_;
    const AVPixFmtDescriptor* desc = d.desc();
    if (!desc || (desc->flags & AV_PIX_FMT_FLAG_HWACCEL)) return {};
    const int w = d.frame->width, h = d.frame->height;
    const auto src_fmt = static_cast<AVPixelFormat>(d.frame->format);
    const bool deep = desc->comp[0].depth > 8;
    const AVPixelFormat dst_fmt = deep ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGB24;

    SwsContext* sws = sws_getContext(
        w, h, src_fmt, w, h, dst_fmt,
        SWS_BICUBIC | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT | SWS_FULL_CHR_H_INP, nullptr,
        nullptr, nullptr);
    if (!sws) return {};

    if (!(desc->flags & AV_PIX_FMT_FLAG_RGB) && desc->nb_components >= 3) {
        // Unspecified matrix: same guess as most players (HD -> BT.709, SD -> BT.601).
        int cs = d.frame->colorspace;
        if (cs == AVCOL_SPC_UNSPECIFIED || cs == AVCOL_SPC_RESERVED)
            cs = h >= 720 ? AVCOL_SPC_BT709 : AVCOL_SPC_SMPTE170M;
        const bool jpeg_fmt = std::strncmp(desc->name, "yuvj", 4) == 0;
        const int src_full = jpeg_fmt || d.frame->color_range == AVCOL_RANGE_JPEG;
        sws_setColorspaceDetails(sws, sws_getCoefficients(cs), src_full,
                                 sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    }

    cv::Mat out(h, w, deep ? CV_16UC4 : CV_8UC3);
    uint8_t* dst[1] = {out.data};
    const int dst_stride[1] = {static_cast<int>(out.step[0])};
    const int rows = sws_scale(sws, d.frame->data, d.frame->linesize, 0, h, dst, dst_stride);
    sws_freeContext(sws);
    if (rows != h) return {};
    if (deep) {
        // Opaque: alpha (if the source had one) is not part of what is shown.
        std::vector<cv::Mat> ch;
        cv::split(out, ch);
        ch[3].setTo(0xFFFF);
        cv::merge(ch, out);
    }
    return out;
}

cv::Mat Decoder::next() {
    auto& d = *impl_;
    if (!d.fmt) return {};
    if (!d.pending && !d.decode()) return {};
    d.pending = false;
    return d.to_bgr();
}

bool Decoder::skip(int n) {
    auto& d = *impl_;
    if (!d.fmt) return false;
    for (; n > 0; --n) {
        if (!d.pending && !d.decode()) return false;
        d.pending = false;
    }
    return true;
}

namespace {

std::string av_err(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

// Owns the output muxer + encoder; cleans up on every exit path.
struct Writer {
    AVFormatContext* oc = nullptr;
    AVCodecContext* enc = nullptr;
    AVStream* st = nullptr;
    AVPacket* pkt = nullptr;
    bool header = false;

    ~Writer() {
        if (pkt) av_packet_free(&pkt);
        if (enc) avcodec_free_context(&enc);
        if (oc) {
            if (!(oc->oformat->flags & AVFMT_NOFILE) && oc->pb) avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    }

    // Drains encoded packets into the muxer. `flush` sends EOF first.
    int write(AVFrame* f) {
        int r = avcodec_send_frame(enc, f);
        if (r < 0) return r;
        for (;;) {
            r = avcodec_receive_packet(enc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return 0;
            if (r < 0) return r;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            r = av_interleaved_write_frame(oc, pkt);
            if (r < 0) return r;
        }
    }
};

// Full-range JPEG formats (yuvj*) hold the same samples as their yuv* twin; only the
// range tag differs. FFV1 rejects yuvj*, so store the yuv* format and tag the range.
AVPixelFormat ffv1_format(AVPixelFormat fmt) {
    switch (fmt) {
        case AV_PIX_FMT_YUVJ420P: return AV_PIX_FMT_YUV420P;
        case AV_PIX_FMT_YUVJ422P: return AV_PIX_FMT_YUV422P;
        case AV_PIX_FMT_YUVJ444P: return AV_PIX_FMT_YUV444P;
        case AV_PIX_FMT_YUVJ440P: return AV_PIX_FMT_YUV440P;
        default: return fmt;
    }
}

}  // namespace

ExportResult Decoder::export_lossless(int first, int last, const std::string& path,
                                      const ExportOptions& opts) {
    auto& d = *impl_;
    auto fail = [](std::string msg) { return ExportResult{ExportStatus::Error, std::move(msg)}; };
    if (!d.fmt) return fail("no video open");
    if (first < 0 || last < first || last >= frame_count()) return fail("invalid frame range");
    if (!seek(first)) return fail("seek failed");

    const AVStream* in = d.fmt->streams[d.stream];
    const AVFrame* f0 = d.frame;  // frame `first`, decoded by seek()
    const int total = last - first + 1;

    Writer w;
    int r = avformat_alloc_output_context2(&w.oc, nullptr, "matroska", path.c_str());
    if (r < 0 || !w.oc) return fail("cannot create muxer: " + av_err(r));
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    if (!codec) return fail("FFV1 encoder not available");
    w.enc = avcodec_alloc_context3(codec);
    w.st = avformat_new_stream(w.oc, nullptr);
    w.pkt = av_packet_alloc();
    if (!w.enc || !w.st || !w.pkt) return fail("out of memory");

    AVCodecContext* e = w.enc;
    e->width = f0->width;
    e->height = f0->height;
    const auto src_fmt = static_cast<AVPixelFormat>(f0->format);
    e->pix_fmt = ffv1_format(src_fmt);  // same samples, no conversion
    e->time_base = in->time_base;
    e->framerate = in->avg_frame_rate;
    e->sample_aspect_ratio = f0->sample_aspect_ratio;
    e->color_range = e->pix_fmt != src_fmt ? AVCOL_RANGE_JPEG : f0->color_range;
    e->colorspace = f0->colorspace;
    e->color_primaries = f0->color_primaries;
    e->color_trc = f0->color_trc;
    e->gop_size = 1;
    if (w.oc->oformat->flags & AVFMT_GLOBALHEADER) e->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set_int(e->priv_data, "level", 3, 0);
    av_opt_set_int(e->priv_data, "coder", 1, 0);
    av_opt_set_int(e->priv_data, "slicecrc", 1, 0);
    if ((r = avcodec_open2(e, codec, nullptr)) < 0)
        return fail("FFV1 cannot encode this pixel format: " + av_err(r));

    if ((r = avcodec_parameters_from_context(w.st->codecpar, e)) < 0) return fail(av_err(r));
    w.st->time_base = e->time_base;
    w.st->avg_frame_rate = in->avg_frame_rate;
    w.st->sample_aspect_ratio = e->sample_aspect_ratio;

    if ((r = avio_open(&w.oc->pb, path.c_str(), AVIO_FLAG_WRITE)) < 0)
        return fail("cannot open output: " + av_err(r));
    auto discard = [&](ExportResult res) {
        w.header = false;
        if (w.oc->pb) avio_closep(&w.oc->pb);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return res;
    };
    if ((r = avformat_write_header(w.oc, nullptr)) < 0)
        return discard(fail("cannot write header: " + av_err(r)));
    w.header = true;

    AVFrame* out = av_frame_alloc();
    if (!out) return discard(fail("out of memory"));
    ExportResult res{ExportStatus::Ok, {}};
    const int64_t base = d.pts[first];
    for (int i = first; i <= last; ++i) {
        if (opts.stop.stop_requested()) {
            res.status = ExportStatus::Cancelled;
            break;
        }
        if (i != first) {
            d.pending = false;
            if (!d.decode()) {
                res = fail("stream ended early at frame " + std::to_string(i));
                break;
            }
        }
        d.pending = false;
        if (d.frame->width != e->width || d.frame->height != e->height ||
            d.frame->format != src_fmt) {
            res = fail("frame format changed mid-stream at frame " + std::to_string(i));
            break;
        }
        av_frame_unref(out);
        av_frame_ref(out, d.frame);
        if (out->format != e->pix_fmt) {
            out->format = e->pix_fmt;
            out->color_range = AVCOL_RANGE_JPEG;
        }
        out->pts = d.pts[i] - base;
        out->pict_type = AV_PICTURE_TYPE_NONE;
        if ((r = w.write(out)) < 0) {
            res = fail("encode/write failed: " + av_err(r));
            break;
        }
        if (opts.progress) opts.progress(i - first + 1, total);
    }
    av_frame_free(&out);

    if (res.status == ExportStatus::Ok) {
        if ((r = w.write(nullptr)) < 0) res = fail("flush failed: " + av_err(r));
        else if ((r = av_write_trailer(w.oc)) < 0) res = fail("cannot finalize: " + av_err(r));
    }
    if (res.status != ExportStatus::Ok) return discard(std::move(res));
    return res;
}

}  // namespace dfd
