// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Adam Bogos
#include "mpeg.hpp"
#include "log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {
void av_log_follow_verbosity() {
    static const bool once = [] {
        av_log_set_level(logging::level >= 1 ? AV_LOG_INFO : AV_LOG_QUIET);
        return true;
    }();
    (void)once;
}
}  // namespace

// Host mixer format (TrapLayer SDL device): interleaved stereo S16 @ 22050 Hz.
static constexpr int kOutRate = 22050;
static constexpr int kOutChannels = 2;

// ---- aspect-fit scaler -------------------------------------------------------
// Bilinear, RGB565 in and out, hand-rolled rather than swscale: the frames are
// already RGB565 so there is nothing for swscale's format machinery to do, and a
// cached SwsContext per movie would add a lifetime to manage across the movie map
// for a scale this cheap (≤640×480 at ≤24fps, integer inner loop).
//
// Weights are 0..256 fixed point. Sampling uses pixel centres — (i+0.5)*src/dst
// − 0.5 — so the image is not shifted half a pixel toward the origin, which is
// visible as a soft edge on one side at these modest ratios.
namespace {

inline void unpack565(uint16_t p, int& r, int& g, int& b) {
    r = (p >> 11) & 0x1f;
    g = (p >> 5) & 0x3f;
    b = p & 0x1f;
}

// One axis of the sample map: for each destination index, the two source indices
// to blend and the weight of the second.
void build_axis(int src, int dst, std::vector<int32_t>& i0,
                std::vector<int32_t>& i1, std::vector<int32_t>& w) {
    i0.resize(dst);
    i1.resize(dst);
    w.resize(dst);
    for (int i = 0; i < dst; ++i) {
        double s = (dst > 1) ? (i + 0.5) * (double)src / dst - 0.5 : 0.0;
        int a = (int)std::floor(s);
        double f = s - a;
        if (a < 0) { a = 0; f = 0.0; }
        if (a > src - 1) { a = src - 1; f = 0.0; }
        int b = a + 1;
        if (b > src - 1) { b = src - 1; }
        i0[i] = a;
        i1[i] = b;
        w[i] = (int32_t)(f * 256.0 + 0.5);
    }
}

}  // namespace

const uint16_t* MpegMovie::fit_frame(const std::vector<uint16_t>& fr,
                                     int dst_w, int dst_h) {
    if (width <= 0 || height <= 0 || dst_w <= 0 || dst_h <= 0) return nullptr;
    if (fr.size() < (size_t)width * (size_t)height) return nullptr;

    if (dst_w != fit_w || dst_h != fit_h) {
        fit_w = dst_w;
        fit_h = dst_h;
        // Fill the tighter axis, preserve aspect, centre the rest.
        double s = std::min((double)dst_w / width, (double)dst_h / height);
        inner_w = std::min(dst_w, std::max(1, (int)(width * s + 0.5)));
        inner_h = std::min(dst_h, std::max(1, (int)(height * s + 0.5)));
        inner_x = (dst_w - inner_w) / 2;
        inner_y = (dst_h - inner_h) / 2;
        // Zeroing here is what blacks the bars. The inner rect is rewritten every
        // frame and the bars never are, so this is the only clear needed — and it
        // must happen on every geometry change, or a previous movie's bars survive.
        fit.assign((size_t)dst_w * (size_t)dst_h, 0);
        build_axis(width, inner_w, map_x0, map_x1, map_wx);
        LOG_V("  [mpeg] fit %dx%d -> %dx%d at +%d,+%d in %dx%d"
                    " (%s %d px)\n",
                    width, height, inner_w, inner_h, inner_x, inner_y, dst_w, dst_h,
                    inner_h < dst_h ? "letterbox" : (inner_w < dst_w ? "pillarbox"
                                                                     : "exact fit"),
                    inner_h < dst_h ? (dst_h - inner_h) / 2 : (dst_w - inner_w) / 2);
    }

    const uint16_t* src = fr.data();
    for (int y = 0; y < inner_h; ++y) {
        double sy = (inner_h > 1) ? (y + 0.5) * (double)height / inner_h - 0.5 : 0.0;
        int ya = (int)std::floor(sy);
        double fy = sy - ya;
        if (ya < 0) { ya = 0; fy = 0.0; }
        if (ya > height - 1) { ya = height - 1; fy = 0.0; }
        int yb = std::min(ya + 1, height - 1);
        const int32_t wy = (int32_t)(fy * 256.0 + 0.5);
        const uint16_t* row_a = src + (size_t)ya * width;
        const uint16_t* row_b = src + (size_t)yb * width;
        uint16_t* out = fit.data() + (size_t)(inner_y + y) * fit_w + inner_x;
        for (int x = 0; x < inner_w; ++x) {
            const int32_t xa = map_x0[x], xb = map_x1[x], wx = map_wx[x];
            int r00, g00, b00, r01, g01, b01, r10, g10, b10, r11, g11, b11;
            unpack565(row_a[xa], r00, g00, b00);
            unpack565(row_a[xb], r01, g01, b01);
            unpack565(row_b[xa], r10, g10, b10);
            unpack565(row_b[xb], r11, g11, b11);
            const int rt = (r00 * (256 - wx) + r01 * wx) >> 8;
            const int gt = (g00 * (256 - wx) + g01 * wx) >> 8;
            const int bt = (b00 * (256 - wx) + b01 * wx) >> 8;
            const int rb = (r10 * (256 - wx) + r11 * wx) >> 8;
            const int gb = (g10 * (256 - wx) + g11 * wx) >> 8;
            const int bb = (b10 * (256 - wx) + b11 * wx) >> 8;
            const int r = (rt * (256 - wy) + rb * wy) >> 8;
            const int g = (gt * (256 - wy) + gb * wy) >> 8;
            const int b = (bt * (256 - wy) + bb * wy) >> 8;
            out[x] = (uint16_t)(((r & 0x1f) << 11) | ((g & 0x3f) << 5) | (b & 0x1f));
        }
    }
    return fit.data();
}

bool MpegStore::load(uint32_t handle, const std::string& host_path) {
    MpegMovie mov;
    AVFormatContext* fmt = nullptr;
    av_log_follow_verbosity();
    if (avformat_open_input(&fmt, host_path.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "  [mpeg] open failed '%s'\n", host_path.c_str());
        return false;
    }
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    int vstream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vstream < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    AVStream* st = fmt->streams[vstream];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        avformat_close_input(&fmt);
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }
    mov.width = ctx->width;
    mov.height = ctx->height;
    if (st->avg_frame_rate.num && st->avg_frame_rate.den)
        mov.fps = av_q2d(st->avg_frame_rate);
    if (mov.fps < 1.0) mov.fps = 15.0;

    SwsContext* sws = sws_getContext(
        ctx->width, ctx->height, ctx->pix_fmt,
        ctx->width, ctx->height, AV_PIX_FMT_RGB565LE,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        avcodec_free_context(&ctx);
        avformat_close_input(&fmt);
        return false;
    }

    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb = av_frame_alloc();
    int rgb_buf_sz = av_image_get_buffer_size(AV_PIX_FMT_RGB565LE, ctx->width, ctx->height, 1);
    std::vector<uint8_t> rgb_buf((size_t)rgb_buf_sz);
    av_image_fill_arrays(rgb->data, rgb->linesize, rgb_buf.data(),
                         AV_PIX_FMT_RGB565LE, ctx->width, ctx->height, 1);

    // ---- Audio stream (optional): decode + resample to host format ----------
    int astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVCodecContext* actx = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* aframe = astream >= 0 ? av_frame_alloc() : nullptr;
    if (astream >= 0) {
        AVStream* ast = fmt->streams[astream];
        const AVCodec* adec = avcodec_find_decoder(ast->codecpar->codec_id);
        if (adec) {
            actx = avcodec_alloc_context3(adec);
            avcodec_parameters_to_context(actx, ast->codecpar);
            if (avcodec_open2(actx, adec, nullptr) < 0) {
                avcodec_free_context(&actx);
            } else {
                // ffmpeg 6+/7 channel-layout API. Out = stereo S16 @ 22050.
                AVChannelLayout out_ch = AV_CHANNEL_LAYOUT_STEREO;
                AVChannelLayout in_ch = actx->ch_layout;
                if (in_ch.nb_channels == 0)
                    av_channel_layout_default(&in_ch, 1);
                if (swr_alloc_set_opts2(&swr, &out_ch, AV_SAMPLE_FMT_S16, kOutRate,
                                        &in_ch, actx->sample_fmt, actx->sample_rate,
                                        0, nullptr) < 0 ||
                    swr_init(swr) < 0) {
                    if (swr) swr_free(&swr);
                    avcodec_free_context(&actx);
                }
            }
        }
    }
    // Resample one decoded audio frame and append S16 stereo to mov.audio.
    auto append_audio = [&](AVFrame* af) {
        if (!swr) return;
        int in_n = af ? af->nb_samples : 0;
        int64_t out_n = swr_get_out_samples(swr, in_n);
        if (out_n <= 0) return;
        uint8_t* buf = nullptr;
        int linesize = 0;
        if (av_samples_alloc(&buf, &linesize, kOutChannels, (int)out_n,
                             AV_SAMPLE_FMT_S16, 0) < 0)
            return;
        const uint8_t** in_data = af ? (const uint8_t**)af->extended_data : nullptr;
        int got = swr_convert(swr, &buf, (int)out_n, in_data, in_n);
        if (got > 0) {
            const int16_t* s = reinterpret_cast<const int16_t*>(buf);
            mov.audio.insert(mov.audio.end(), s, s + (size_t)got * kOutChannels);
        }
        av_freep(&buf);
    };

    AVPacket* pkt = av_packet_alloc();
    const size_t max_frames = 60 * 30;  // safety cap (~30s @ 60fps)
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == astream && actx && swr) {
            if (avcodec_send_packet(actx, pkt) == 0)
                while (avcodec_receive_frame(actx, aframe) == 0)
                    append_audio(aframe);
            av_packet_unref(pkt);
            continue;
        }
        if (pkt->stream_index != vstream) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(ctx, pkt) < 0) {
            av_packet_unref(pkt);
            continue;
        }
        av_packet_unref(pkt);
        while (avcodec_receive_frame(ctx, frame) == 0) {
            sws_scale(sws, frame->data, frame->linesize, 0, ctx->height,
                      rgb->data, rgb->linesize);
            std::vector<uint16_t> out((size_t)mov.width * (size_t)mov.height);
            // linesize may have padding — copy row by row
            const uint8_t* src = rgb->data[0];
            int src_pitch = rgb->linesize[0];
            for (int y = 0; y < mov.height; ++y) {
                std::memcpy(out.data() + (size_t)y * (size_t)mov.width,
                            src + (size_t)y * (size_t)src_pitch,
                            (size_t)mov.width * 2);
            }
            mov.frames.push_back(std::move(out));
            if (mov.frames.size() >= max_frames) break;
        }
        if (mov.frames.size() >= max_frames) break;
    }
    // Drain decoder
    avcodec_send_packet(ctx, nullptr);
    while (avcodec_receive_frame(ctx, frame) == 0) {
        sws_scale(sws, frame->data, frame->linesize, 0, ctx->height,
                  rgb->data, rgb->linesize);
        std::vector<uint16_t> out((size_t)mov.width * (size_t)mov.height);
        const uint8_t* src = rgb->data[0];
        int src_pitch = rgb->linesize[0];
        for (int y = 0; y < mov.height; ++y)
            std::memcpy(out.data() + (size_t)y * (size_t)mov.width,
                        src + (size_t)y * (size_t)src_pitch,
                        (size_t)mov.width * 2);
        mov.frames.push_back(std::move(out));
        if (mov.frames.size() >= max_frames) break;
    }

    // Drain the audio decoder + resampler tail.
    if (actx && swr) {
        avcodec_send_packet(actx, nullptr);
        while (avcodec_receive_frame(actx, aframe) == 0)
            append_audio(aframe);
        append_audio(nullptr);   // flush swr's internal buffer
    }
    if (!mov.audio.empty()) {
        mov.has_audio = true;
        mov.samplerate = kOutRate;
        mov.channels = kOutChannels;
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&rgb);
    if (aframe) av_frame_free(&aframe);
    if (swr) swr_free(&swr);
    if (actx) avcodec_free_context(&actx);
    sws_freeContext(sws);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    if (mov.frames.empty()) {
        std::fprintf(stderr, "  [mpeg] no frames in '%s'\n", host_path.c_str());
        return false;
    }
    LOG_V("  [mpeg] decoded '%s' %dx%d %zu frames @ %.1f fps, audio %zu samp (%.1fs)\n",
                host_path.c_str(), mov.width, mov.height, mov.frames.size(), mov.fps,
                mov.audio.size() / (size_t)kOutChannels,
                mov.has_audio ? (double)(mov.audio.size() / kOutChannels) / kOutRate : 0.0);
    movies_[handle] = std::move(mov);
    return true;
}

void MpegStore::erase(uint32_t handle) { movies_.erase(handle); }

MpegMovie* MpegStore::get(uint32_t handle) {
    auto it = movies_.find(handle);
    return it == movies_.end() ? nullptr : &it->second;
}

void MpegStore::clear() { movies_.clear(); }
