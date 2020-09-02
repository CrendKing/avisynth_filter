#pragma once

#include "pch.h"


namespace AvsFilter {

// https://github.com/Nevcairiel/LAVFilters/blob/master/developer_info/IMediaSideData.h
interface __declspec(uuid("F940AE7F-48EB-4377-806C-8FC48CAB2292")) IMediaSideData : public IUnknown {
    STDMETHOD(SetSideData)(GUID guidType, const BYTE *pData, size_t size) PURE;
    STDMETHOD(GetSideData)(GUID guidType, const BYTE **pData, size_t *pSize) PURE;
};

#pragma pack(push, 1)
struct MediaSideDataHDR {
    // coordinates of the primaries, in G-B-R order
    double display_primaries_x[3];
    double display_primaries_y[3];
    // white point
    double white_point_x;
    double white_point_y;
    // luminance
    double max_display_mastering_luminance;
    double min_display_mastering_luminance;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct MediaSideDataHDRContentLightLevel {
    // maximum content light level (cd/m2)
    unsigned int MaxCLL;

    // maximum frame average light level (cd/m2)
    unsigned int MaxFALL;
};
#pragma pack(pop)

#pragma pack(push, 1)
struct MediaSideData3DOffset {
    // Number of valid offsets (up to 32)
    int offset_count;

    // Offset Value, can be positive or negative
    // positive values offset closer to the viewer (move right on the left view, left on the right view)
    // negative values offset further away from the viewer (move left on the left view, right on the right view)
    int offset[32];
};
#pragma pack(pop)

class HDRSideData {
public:
    auto STDMETHODCALLTYPE StoreSideData(GUID guidType, const BYTE *pData, size_t size) -> HRESULT;
    auto STDMETHODCALLTYPE RetrieveSideData(GUID guidType, const BYTE **pData, size_t *pSize) const -> HRESULT;

    auto Read(IMediaSideData *from) -> void;
    auto Write(IMediaSideData *to) const -> void;

    auto GetHDRData() const -> std::optional<const BYTE *>;
    auto GetContentLightLevelData() const -> std::optional<const BYTE *>;
    auto Get3DOffsetData() const -> std::optional<const BYTE *>;

private:
    // not using std::optional here because std::optional<T &> is not available in C++17
    auto GetDataByGUID(GUID guidType) -> std::vector<BYTE> *;

    std::vector<BYTE> _hdrData;
    std::vector<BYTE> _hdrContentLightLevelData;
    std::vector<BYTE> _hdr3DOffsetData;
};

}