#pragma once

#include "pch.h"


auto AVSC_CC filter_get_frame(AVS_FilterInfo *fi, int frameNb) -> AVS_VideoFrame *;
auto AVSC_CC filter_get_parity(AVS_FilterInfo *, int frameNb) -> int;
