#include "pch.h"
#include "buffer_handler.h"
#include "format.h"
#include "g_variables.h"


auto BufferHandler::Reset(const CLSID &inFormat, const AVS_VideoInfo *videoInfo) -> void {
    _formatIndex = Format::LookupInput(inFormat);
    _videoInfo = videoInfo;

    GarbageCollect(LONGLONG_MAX);
}

auto BufferHandler::GetNearestFrame(REFERENCE_TIME frameTime) -> AVS_VideoFrame * {
    /*
    Instead of searching for a frame that's closest to the requested time, we look for the earliest frame after.
    For example, suppose we have 3 frames in buffer, whose time are 0, 10 and 20.
    When requested at time
        * 10 -> frame 2
        * 6  -> frame 1
        * 12 -> frame 2
        * 19 -> frame 2
        * 21 -> frame 3
    This design is meant to diversify the frames returned when AviSynth's timestamp is off from the filter graph,
    even though they still share the same fps. Because AviSynth works on frame number while DirectShow works on reference time.

    For instance, suppose the average time per frame is 1000. The first frame arrived from upstream at 500.
    These will be frame times in buffer: 500, 1500, 2500, etc.
    These will be the requested times: 0, 1000, 2000, etc.
    */

    std::shared_lock<std::shared_mutex> lock(_mutex);

    AVS_VideoFrame *frame = nullptr;
    for (const FrameInfo &info : _frameBuffer) {
        if (frameTime >= info.time) {
            frame = info.frame;
            break;
        }
    }

    if (frame == nullptr) {
        frame = _frameBuffer.front().frame;
    }

    return avs_copy_video_frame(frame);
}

/*
"unit stride" means the stride in unit of the size for each pixel. For example, for 8-bit char-sized buffers,
x unit stride means the stride has x * 1 bytes. For word-sized buffers (10-bit, 16-bit, etc), x unit stride means x * 2 bytes.
*/

auto BufferHandler::CreateFrame(REFERENCE_TIME frameTime, const BYTE *srcBuffer, long srcUnitStride) -> void {
    AVS_VideoFrame *frame = avs_new_video_frame(g_env, _videoInfo);

    BYTE *dstSlices[] = { avs_get_write_ptr_p(frame, AVS_PLANAR_Y), avs_get_write_ptr_p(frame, AVS_PLANAR_U), avs_get_write_ptr_p(frame, AVS_PLANAR_V) };
    const int dstStrides[] = { avs_get_pitch_p(frame, AVS_PLANAR_Y), avs_get_pitch_p(frame, AVS_PLANAR_U) };

    Format::Copy(_formatIndex, srcBuffer, srcUnitStride, dstSlices, dstStrides, _videoInfo->width, _videoInfo->height);

    std::lock_guard<std::shared_mutex> lock(_mutex);
    _frameBuffer.emplace_front(FrameInfo { frameTime, frame });
}

auto BufferHandler::WriteSample(const AVS_VideoFrame *srcFrame, BYTE *dstBuffer, long dstUnitStride) const -> void {
    const BYTE *srcSlices[] = { avs_get_read_ptr_p(srcFrame, AVS_PLANAR_Y), avs_get_read_ptr_p(srcFrame, AVS_PLANAR_U), avs_get_read_ptr_p(srcFrame, AVS_PLANAR_V) };
    const int srcStrides[] = { avs_get_pitch_p(srcFrame, AVS_PLANAR_Y), avs_get_pitch_p(srcFrame, AVS_PLANAR_U) };

    Format::Copy(_formatIndex, srcSlices, srcStrides, dstBuffer, dstUnitStride, _videoInfo->width, _videoInfo->height);
}

auto BufferHandler::GarbageCollect(REFERENCE_TIME streamTime) -> void {
    std::lock_guard<std::shared_mutex> lock(_mutex);

    while (_frameBuffer.size() > 1 && _frameBuffer.back().time < streamTime) {
        avs_release_video_frame(_frameBuffer.back().frame);
        _frameBuffer.pop_back();
    }
}
