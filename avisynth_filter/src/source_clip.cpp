#include "pch.h"
#include "source_clip.h"
#include "buffer_handler.h"


auto AVSC_CC filter_get_frame(AVS_FilterInfo *fi, int frameNb) -> AVS_VideoFrame * {
    const REFERENCE_TIME frameTime = frameNb * fi->vi.fps_denominator * UNITS / fi->vi.fps_numerator;
    return reinterpret_cast<BufferHandler *>(fi->user_data)->GetNearestFrame(frameTime);
}

auto AVSC_CC filter_get_parity(AVS_FilterInfo *, int frameNb) -> int {
    return 0;
}