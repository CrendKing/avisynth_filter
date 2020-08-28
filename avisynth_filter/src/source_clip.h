#pragma once

#include "pch.h"
#include "IMediaSideData.h"

class SourceClip : public IClip {
public:
    struct SideData {
        bool hasHDR;
        bool hasHDR_CLL;
        bool hasOffset3d;
        MediaSideDataHDR hdr;
        MediaSideDataHDRContentLightLevel hdr_cll;
        MediaSideData3DOffset offset3d;

        auto Read(IMediaSideData* rw) -> void;
        auto Write(IMediaSideData* rw) -> void;
    };

    struct FrameInfo {
        int frameNb;
        PVideoFrame frame;
        REFERENCE_TIME startTime;
        REFERENCE_TIME stopTime;
        std::shared_ptr<SideData> sideData;
    };

    explicit SourceClip(const VideoInfo &videoInfo);

    auto __stdcall GetFrame(int frameNb, IScriptEnvironment *env) -> PVideoFrame override;
    auto __stdcall GetParity(int frameNb) -> bool override;
    auto __stdcall GetAudio(void *buf, int64_t start, int64_t count, IScriptEnvironment *env) -> void override;
    auto __stdcall SetCacheHints(int cachehints, int frame_range) -> int override;
    auto __stdcall GetVideoInfo() -> const VideoInfo & override;

    auto PushBackFrame(PVideoFrame frame, REFERENCE_TIME startTime, SideData *sideData = nullptr) -> int;
    auto GetFrontFrame() const -> std::optional<FrameInfo>;
    auto PopFrontFrame() -> void;
    auto FlushOnNextInput() -> void;

    auto GetBufferSize() const -> int;
    auto GetMaxAccessedFrameNb() const -> int;

private:
    const VideoInfo &_videoInfo;
    mutable std::mutex _bufferMutex;
    std::deque<FrameInfo> _frameBuffer;
    bool _flushOnNextInput;
    int _maxRequestedFrameNb;
};
