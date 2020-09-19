#include "pch.h"
#include "side_data.h"
#include "constants.h"


namespace AvsFilter {

auto STDMETHODCALLTYPE HDRSideData::StoreSideData(GUID guidType, const BYTE *pData, size_t size) -> HRESULT {
    std::vector<BYTE> *data = GetDataByGUID(guidType);
    if (data == nullptr) {
        return E_FAIL;
    }

    data->resize(size);
    data->assign(pData, pData + size);

    return S_OK;
}

auto STDMETHODCALLTYPE HDRSideData::RetrieveSideData(GUID guidType, const BYTE **pData, size_t *pSize) const -> HRESULT {
    if (pData == nullptr || pSize == nullptr) {
        return E_FAIL;
    }

    const std::vector<BYTE> *data = const_cast<HDRSideData *>(this)->GetDataByGUID(guidType);
    if (data == nullptr || data->empty()) {
        return E_FAIL;
    }

    *pData = data->data();
    *pSize = data->size();
    return S_OK;
}

auto HDRSideData::Read(IMediaSideData *from) -> void {
    const BYTE *data;
    size_t dataSize;

    if (SUCCEEDED(from->GetSideData(IID_MediaSideDataHDR, &data, &dataSize))) {
        StoreSideData(IID_MediaSideDataHDR, data, dataSize);
    }

    if (SUCCEEDED(from->GetSideData(IID_MediaSideDataHDRContentLightLevel, &data, &dataSize))) {
        StoreSideData(IID_MediaSideDataHDRContentLightLevel, data, dataSize);
    }

    if (SUCCEEDED(from->GetSideData(IID_MediaSideData3DOffset, &data, &dataSize))) {
        StoreSideData(IID_MediaSideData3DOffset, data, dataSize);
    }
}

auto HDRSideData::Write(IMediaSideData *to) const -> void {
    if (!_hdrData.empty()) {
        to->SetSideData(IID_MediaSideDataHDR, _hdrData.data(), _hdrData.size());
    }

    if (!_hdrContentLightLevelData.empty()) {
        to->SetSideData(IID_MediaSideDataHDRContentLightLevel, _hdrContentLightLevelData.data(), _hdrContentLightLevelData.size());
    }

    if (!_hdr3DOffsetData.empty()) {
        to->SetSideData(IID_MediaSideData3DOffset, _hdr3DOffsetData.data(), _hdr3DOffsetData.size());
    }
}

auto HDRSideData::GetHDRData() const -> std::optional<const BYTE *> {
    if (_hdrData.empty()) {
        return std::nullopt;
    }

    return _hdrData.data();
}

auto HDRSideData::GetContentLightLevelData() const -> std::optional<const BYTE *> {
    if (_hdrContentLightLevelData.empty()) {
        return std::nullopt;
    }

    return _hdrContentLightLevelData.data();

}

auto HDRSideData::Get3DOffsetData() const -> std::optional<const BYTE *> {
    if (_hdr3DOffsetData.empty()) {
        return std::nullopt;
    }

    return _hdr3DOffsetData.data();

}

auto HDRSideData::GetDataByGUID(GUID guidType) -> std::vector<BYTE> * {
    if (guidType == IID_MediaSideDataHDR) {
        return &_hdrData;
    }

    if (guidType == IID_MediaSideDataHDRContentLightLevel) {
        return &_hdrContentLightLevelData;
    }

    if (guidType == IID_MediaSideData3DOffset) {
        return &_hdr3DOffsetData;
    }

    return nullptr;
}

}
