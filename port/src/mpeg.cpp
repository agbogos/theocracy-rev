#include "mpeg.hpp"
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

// Host mixer format (TrapLayer SDL device): interleaved stereo S16 @ 22050 Hz.
static constexpr int kOutRate = 22050;
static constexpr int kOutChannels = 2;

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
    std::printf("  [mpeg] decoded '%s' %dx%d %zu frames @ %.1f fps, audio %zu samp (%.1fs)\n",
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
