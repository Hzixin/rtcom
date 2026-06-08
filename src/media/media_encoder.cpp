#include "media_encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>
}

#include <glog/logging.h>
#include <cstring>

namespace rtcom {

MediaEncoder::MediaEncoder(CodecType codec_type) : codec_type_(codec_type) {}
MediaEncoder::~MediaEncoder() { Close(); }

bool MediaEncoder::Initialize(int bitrate, int sample_rate, int channels,
                               int width, int height, int fps) {
    sample_rate_ = sample_rate; channels_ = channels;
    width_ = width; height_ = height; fps_ = fps;

    AVCodecID codec_id = IsAudio() ? AV_CODEC_ID_AAC : AV_CODEC_ID_H264;
    const AVCodec* codec = avcodec_find_encoder(codec_id);
    if (!codec) { LOG(ERROR) << "Encoder not found"; return false; }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) return false;

    if (IsAudio()) {
        ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
        ctx_->sample_rate = sample_rate_;
        ctx_->channel_layout = channels_ == 2 ? AV_CH_LAYOUT_STEREO : AV_CH_LAYOUT_MONO;
        ctx_->bit_rate = bitrate;
    } else {
        ctx_->width = width_; ctx_->height = height_;
        ctx_->time_base = AVRational{1, fps_};
        ctx_->framerate = AVRational{fps_, 1};
        ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx_->bit_rate = bitrate;
        ctx_->gop_size = fps_; ctx_->max_b_frames = 0;
        av_opt_set(ctx_->priv_data, "preset", "fast", 0);
        av_opt_set(ctx_->priv_data, "tune", "zerolatency", 0);
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0) { Close(); return false; }

    frame_ = av_frame_alloc();
    if (!IsAudio()) {
        frame_->format = ctx_->pix_fmt;
        frame_->width = width_; frame_->height = height_;
        av_frame_get_buffer(frame_, 0);
    }
    pkt_ = av_packet_alloc();
    pts_ = 0;
    LOG(INFO) << "Encoder: " << CodecTypeToString(codec_type_) << " @" << bitrate << "bps";
    return true;
}

void MediaEncoder::Close() {
    if (pkt_) { av_packet_free(&pkt_); }
    if (frame_) { av_frame_free(&frame_); }
    if (ctx_) { avcodec_free_context(&ctx_); }
}

bool MediaEncoder::EncodeAudio(const int16_t* pcm_data, size_t sample_count,
                                std::vector<uint8_t>& encoded) {
    if (!ctx_ || !frame_ || !pkt_) return false;
    frame_->nb_samples = static_cast<int>(sample_count);
    frame_->format = ctx_->sample_fmt;
    frame_->channel_layout = ctx_->channel_layout;
    frame_->sample_rate = ctx_->sample_rate;
    av_frame_get_buffer(frame_, 0);
    memcpy(frame_->data[0], pcm_data, sample_count * channels_ * sizeof(int16_t));
    frame_->pts = pts_++;

    if (avcodec_send_frame(ctx_, frame_) < 0) return false;
    av_frame_unref(frame_);
    if (avcodec_receive_packet(ctx_, pkt_) == 0) {
        encoded.assign(pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
        return true;
    }
    return false;
}

bool MediaEncoder::EncodeVideo(const uint8_t* yuv_data, size_t yuv_len,
                                std::vector<uint8_t>& encoded) {
    if (!ctx_ || !frame_ || !pkt_) return false;
    int y_size = width_ * height_;
    memcpy(frame_->data[0], yuv_data, std::min(static_cast<size_t>(y_size), yuv_len));
    if (yuv_len > static_cast<size_t>(y_size)) {
        size_t uv_size = y_size / 4;
        memcpy(frame_->data[1], yuv_data + y_size, std::min(uv_size, yuv_len - y_size));
        if (yuv_len > y_size + uv_size) {
            memcpy(frame_->data[2], yuv_data + y_size + uv_size,
                   std::min(uv_size, yuv_len - y_size - uv_size));
        }
    }
    frame_->pts = pts_++;
    if (avcodec_send_frame(ctx_, frame_) < 0) return false;
    av_frame_unref(frame_);
    if (avcodec_receive_packet(ctx_, pkt_) == 0) {
        encoded.assign(pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
        return true;
    }
    return false;
}

bool MediaEncoder::Flush(std::vector<uint8_t>& encoded) {
    if (!ctx_ || !pkt_) return false;
    if (avcodec_send_frame(ctx_, nullptr) < 0) return false;
    if (avcodec_receive_packet(ctx_, pkt_) == 0) {
        encoded.assign(pkt_->data, pkt_->data + pkt_->size);
        av_packet_unref(pkt_);
        return true;
    }
    return false;
}

} // namespace rtcom
