#include "media_capture.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <glog/logging.h>

namespace rtcom {

MediaCapture::MediaCapture() { avformat_network_init(); }
MediaCapture::~MediaCapture() { Close(); }

void MediaCapture::FindStreams() {
    for (unsigned i = 0; i < fmt_ctx_->nb_streams; ++i) {
        AVCodecParameters* params = fmt_ctx_->streams[i]->codecpar;
        if (params->codec_type == AVMEDIA_TYPE_AUDIO && audio_stream_idx_ < 0) {
            audio_stream_idx_ = static_cast<int>(i);
            has_audio_ = true;
            audio_sample_rate_ = params->sample_rate;
            audio_channels_ = params->channels;
        } else if (params->codec_type == AVMEDIA_TYPE_VIDEO && video_stream_idx_ < 0) {
            video_stream_idx_ = static_cast<int>(i);
            has_video_ = true;
            video_width_ = params->width;
            video_height_ = params->height;
            AVRational fps = fmt_ctx_->streams[i]->avg_frame_rate;
            video_fps_ = fps.den ? av_q2d(fps) : 30.0;
        }
    }
}

bool MediaCapture::OpenCodec(int stream_idx, AVCodecContext** ctx) {
    AVCodecParameters* params = fmt_ctx_->streams[stream_idx]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(params->codec_id);
    if (!codec) return false;
    *ctx = avcodec_alloc_context3(codec);
    if (!*ctx || avcodec_parameters_to_context(*ctx, params) < 0) return false;
    return avcodec_open2(*ctx, codec, nullptr) >= 0;
}

bool MediaCapture::OpenFile(const std::string& filepath) {
    if (avformat_open_input(&fmt_ctx_, filepath.c_str(), nullptr, nullptr) < 0) {
        LOG(ERROR) << "Cannot open: " << filepath;
        return false;
    }
    if (avformat_find_stream_info(fmt_ctx_, nullptr) < 0) {
        Close(); return false;
    }
    FindStreams();
    if (audio_stream_idx_ >= 0) OpenCodec(audio_stream_idx_, &audio_codec_ctx_);
    if (video_stream_idx_ >= 0) OpenCodec(video_stream_idx_, &video_codec_ctx_);
    if (fmt_ctx_->duration > 0) {
        duration_ms_ = static_cast<int64_t>(fmt_ctx_->duration / (double)AV_TIME_BASE * 1000.0);
    }
    LOG(INFO) << "Opened " << filepath << ": audio=" << has_audio_ << " video=" << has_video_;
    return true;
}

bool MediaCapture::OpenDevice(const std::string& device, CodecType type) {
    AVInputFormat* fmt = nullptr;
#ifdef __linux__
    if (type == CodecType::kVideoH264) fmt = av_find_input_format("v4l2");
    else if (type == CodecType::kAudioAAC) fmt = av_find_input_format("alsa");
#endif
    if (avformat_open_input(&fmt_ctx_, device.c_str(), fmt, nullptr) < 0) {
        LOG(ERROR) << "Cannot open device: " << device;
        return false;
    }
    FindStreams();
    if (audio_stream_idx_ >= 0) OpenCodec(audio_stream_idx_, &audio_codec_ctx_);
    if (video_stream_idx_ >= 0) OpenCodec(video_stream_idx_, &video_codec_ctx_);
    return true;
}

void MediaCapture::Close() {
    if (audio_codec_ctx_) { avcodec_free_context(&audio_codec_ctx_); }
    if (video_codec_ctx_) { avcodec_free_context(&video_codec_ctx_); }
    if (fmt_ctx_) { avformat_close_input(&fmt_ctx_); }
    has_audio_ = false; has_video_ = false;
}

bool MediaCapture::ReadPacket(std::vector<uint8_t>& out_data, CodecType& out_type) {
    if (!fmt_ctx_) return false;
    AVPacket* pkt = av_packet_alloc();
    if (av_read_frame(fmt_ctx_, pkt) < 0) {
        av_packet_free(&pkt);
        return false;
    }
    if (pkt->stream_index == audio_stream_idx_) {
        out_type = CodecType::kAudioAAC;
    } else if (pkt->stream_index == video_stream_idx_) {
        out_type = CodecType::kVideoH264;
    } else {
        av_packet_free(&pkt);
        return ReadPacket(out_data, out_type);
    }
    out_data.assign(pkt->data, pkt->data + pkt->size);
    av_packet_free(&pkt);
    return true;
}

} // namespace rtcom
