#include "media_decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <glog/logging.h>
#include <cstring>
#include <algorithm>

namespace rtcom {

MediaDecoder::MediaDecoder(CodecType codec_type) : codec_type_(codec_type) {}
MediaDecoder::~MediaDecoder() { Close(); }

bool MediaDecoder::Initialize() {
    AVCodecID codec_id = IsAudio() ? AV_CODEC_ID_AAC : AV_CODEC_ID_H264;
    const AVCodec* codec = avcodec_find_decoder(codec_id);
    if (!codec) { LOG(ERROR) << "Decoder not found"; return false; }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_ || avcodec_open2(ctx_, codec, nullptr) < 0) {
        LOG(ERROR) << "Cannot open decoder";
        Close(); return false;
    }

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    initialized_ = true;
    LOG(INFO) << "Decoder: " << CodecTypeToString(codec_type_);
    return true;
}

void MediaDecoder::Close() {
    if (pkt_) { av_packet_free(&pkt_); }
    if (frame_) { av_frame_free(&frame_); }
    if (ctx_) { avcodec_free_context(&ctx_); }
    initialized_ = false;
}

bool MediaDecoder::Decode(const uint8_t* encoded_data, size_t encoded_len,
                          std::vector<uint8_t>& raw_data, int& out_width, int& out_height) {
    if (!initialized_ || !encoded_data || encoded_len == 0) return false;

    pkt_->data = const_cast<uint8_t*>(encoded_data);
    pkt_->size = static_cast<int>(encoded_len);

    if (avcodec_send_packet(ctx_, pkt_) < 0) return false;

    if (avcodec_receive_frame(ctx_, frame_) == 0) {
        out_width = frame_->width;
        out_height = frame_->height;
        int y_size = out_width * out_height;
        int uv_size = y_size / 4;
        raw_data.resize(y_size + 2 * uv_size);

        for (int i = 0; i < out_height; ++i)
            memcpy(raw_data.data() + i * out_width,
                   frame_->data[0] + i * frame_->linesize[0], out_width);
        int uv_h = out_height / 2, uv_w = out_width / 2;
        for (int i = 0; i < uv_h; ++i)
            memcpy(raw_data.data() + y_size + i * uv_w,
                   frame_->data[1] + i * frame_->linesize[1], uv_w);
        for (int i = 0; i < uv_h; ++i)
            memcpy(raw_data.data() + y_size + uv_size + i * uv_w,
                   frame_->data[2] + i * frame_->linesize[2], uv_w);

        av_frame_unref(frame_);
        av_packet_unref(pkt_);
        return true;
    }
    av_packet_unref(pkt_);
    return false;
}

bool MediaDecoder::DecodeAudio(const uint8_t* encoded_data, size_t encoded_len,
                                std::vector<int16_t>& pcm_samples) {
    if (!initialized_ || !encoded_data || encoded_len == 0) return false;

    pkt_->data = const_cast<uint8_t*>(encoded_data);
    pkt_->size = static_cast<int>(encoded_len);

    if (avcodec_send_packet(ctx_, pkt_) < 0) return false;

    if (avcodec_receive_frame(ctx_, frame_) == 0) {
        int samples = frame_->nb_samples;
        int channels = frame_->channels;
        pcm_samples.resize(samples * channels);
        memcpy(pcm_samples.data(), frame_->data[0], samples * channels * sizeof(int16_t));
        av_frame_unref(frame_);
        av_packet_unref(pkt_);
        return true;
    }
    av_packet_unref(pkt_);
    return false;
}

} // namespace rtcom
