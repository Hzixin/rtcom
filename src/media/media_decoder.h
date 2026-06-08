#pragma once

#include "common/types.h"
#include <vector>
#include <cstdint>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace rtcom {

class MediaDecoder {
public:
    explicit MediaDecoder(CodecType codec_type);
    ~MediaDecoder();

    bool Initialize();
    void Close();

    bool Decode(const uint8_t* encoded_data, size_t encoded_len,
                std::vector<uint8_t>& raw_data, int& out_width, int& out_height);
    bool DecodeAudio(const uint8_t* encoded_data, size_t encoded_len,
                     std::vector<int16_t>& pcm_samples);

    CodecType GetCodecType() const { return codec_type_; }
    bool IsAudio() const { return codec_type_ == CodecType::kAudioAAC ||
                                  codec_type_ == CodecType::kAudioPCMU ||
                                  codec_type_ == CodecType::kAudioPCMA; }

private:
    CodecType codec_type_;
    AVCodecContext* ctx_{nullptr};
    AVFrame* frame_{nullptr};
    AVPacket* pkt_{nullptr};
    bool initialized_{false};
};

} // namespace rtcom
