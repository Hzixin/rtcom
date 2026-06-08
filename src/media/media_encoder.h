#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace rtcom {

class MediaEncoder {
public:
    explicit MediaEncoder(CodecType codec_type);
    ~MediaEncoder();

    bool Initialize(int bitrate, int sample_rate = 44100, int channels = 1,
                    int width = 640, int height = 480, int fps = 30);
    void Close();

    bool EncodeAudio(const int16_t* pcm_data, size_t sample_count,
                     std::vector<uint8_t>& encoded);
    bool EncodeVideo(const uint8_t* yuv_data, size_t yuv_len,
                     std::vector<uint8_t>& encoded);
    bool Flush(std::vector<uint8_t>& encoded);

    CodecType GetCodecType() const { return codec_type_; }
    bool IsAudio() const { return codec_type_ == CodecType::kAudioAAC ||
                                  codec_type_ == CodecType::kAudioPCMU ||
                                  codec_type_ == CodecType::kAudioPCMA; }

private:
    CodecType codec_type_;
    AVCodecContext* ctx_{nullptr};
    AVFrame* frame_{nullptr};
    AVPacket* pkt_{nullptr};

    int sample_rate_{44100};
    int channels_{1};
    int width_{640};
    int height_{480};
    int fps_{30};
    int64_t pts_{0};
};

} // namespace rtcom
