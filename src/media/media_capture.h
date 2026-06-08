#pragma once

#include "common/types.h"
#include <string>
#include <memory>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;

namespace rtcom {

class MediaCapture {
public:
    MediaCapture();
    ~MediaCapture();

    bool OpenFile(const std::string& filepath);
    bool OpenDevice(const std::string& device, CodecType type);
    void Close();

    bool ReadPacket(std::vector<uint8_t>& out_data, CodecType& out_type);

    int AudioStreamIndex() const { return audio_stream_idx_; }
    int VideoStreamIndex() const { return video_stream_idx_; }
    bool HasAudio() const { return has_audio_; }
    bool HasVideo() const { return has_video_; }

    int64_t DurationMs() const { return duration_ms_; }
    int AudioSampleRate() const { return audio_sample_rate_; }
    int AudioChannels() const { return audio_channels_; }
    int VideoWidth() const { return video_width_; }
    int VideoHeight() const { return video_height_; }
    double VideoFps() const { return video_fps_; }

private:
    AVFormatContext* fmt_ctx_{nullptr};
    AVCodecContext* audio_codec_ctx_{nullptr};
    AVCodecContext* video_codec_ctx_{nullptr};

    int audio_stream_idx_{-1};
    int video_stream_idx_{-1};
    bool has_audio_{false};
    bool has_video_{false};

    int64_t duration_ms_{0};
    int audio_sample_rate_{0};
    int audio_channels_{0};
    int video_width_{0};
    int video_height_{0};
    double video_fps_{0.0};

    bool OpenCodec(int stream_idx, AVCodecContext** ctx);
    void FindStreams();
};

} // namespace rtcom
