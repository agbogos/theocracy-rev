#include "mpeg.hpp"
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

bool MpegStore::load(uint32_t handle, const std::string& host_path) {
    MpegMovie mov;
    AVFormatContext* fmt = nullptr;
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

    AVPacket* pkt = av_packet_alloc();
    const size_t max_frames = 60 * 30;  // safety cap (~30s @ 60fps)
    while (av_read_frame(fmt, pkt) >= 0) {
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

    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&rgb);
    sws_freeContext(sws);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);

    if (mov.frames.empty()) {
        std::fprintf(stderr, "  [mpeg] no frames in '%s'\n", host_path.c_str());
        return false;
    }
    std::printf("  [mpeg] decoded '%s' %dx%d %zu frames @ %.1f fps\n",
                host_path.c_str(), mov.width, mov.height, mov.frames.size(), mov.fps);
    movies_[handle] = std::move(mov);
    return true;
}

void MpegStore::erase(uint32_t handle) { movies_.erase(handle); }

MpegMovie* MpegStore::get(uint32_t handle) {
    auto it = movies_.find(handle);
    return it == movies_.end() ? nullptr : &it->second;
}

void MpegStore::clear() { movies_.clear(); }
