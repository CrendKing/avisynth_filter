// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "pch.h"
#include "frameserver.h"
#include "api.h"
#include "constants.h"


namespace SynthFilter {

static constexpr const char *VPS_SOURCE_NODE_NAME         = "VpsFilterSource";
static constexpr const char *VPS_DISCONNECT_VARIABLE_NAME = "VpsFilterDisconnect";

static auto VS_CC SourceInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) -> void {
    AVSF_VS_API->setVideoInfo(&FrameServer::GetInstance().GetSourceVideoInfo(), 1, node);
}

static auto VS_CC SourceGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) -> const VSFrameRef * {
    FrameHandler &frameHandler = MainScriptInstance::GetInstance().GetFrameHandler();
    return AVSF_VS_API->cloneFrameRef(frameHandler.GetSourceFrame(n));
}

auto ScriptInstance::StopScript() -> void {
    if (_scriptClip != nullptr) {
        Environment::GetInstance().Log(L"Release script clip: %p", _scriptClip);
        AVSF_VS_API->freeNode(_scriptClip);
        _scriptClip = nullptr;
    }
}

ScriptInstance::ScriptInstance() {
    vsscript_createScript(&_vsScript);
}

ScriptInstance::~ScriptInstance() {
    StopScript();
    vsscript_freeScript(_vsScript);
}

/**
 * Create new script clip with specified media type.
 */
auto ScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    StopScript();

    FrameServer::GetInstance()._sourceVideoInfo = Format::GetVideoFormat(mediaType, this).videoInfo;

    VSMap *filterInputs = AVSF_VS_API->createMap();
    VSMap *filterOutputs = AVSF_VS_API->createMap();

    AVSF_VS_API->createFilter(filterInputs, filterOutputs, "VpsFilter_Source", SourceInit, SourceGetFrame, nullptr, fmParallel, nfNoCache, nullptr, vsscript_getCore(_vsScript));
    VSNodeRef *sourceClip = AVSF_VS_API->propGetNode(filterOutputs, "clip", 0, nullptr);
    AVSF_VS_API->propSetNode(filterInputs, VPS_SOURCE_NODE_NAME, sourceClip, 0);
    vsscript_setVariable(_vsScript, filterInputs);

    AVSF_VS_API->freeMap(filterOutputs);
    AVSF_VS_API->freeMap(filterInputs);

    _errorString.clear();

    bool toDisconnect = false;

    if (!FrameServer::GetInstance()._scriptPath.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(FrameServer::GetInstance()._scriptPath);

        if (vsscript_evaluateFile(&_vsScript, utf8Filename.c_str(), efSetWorkingDir) == 0) {
            _scriptClip = vsscript_getOutput(_vsScript, 0);

            VSMap *scriptOutputs = AVSF_VS_API->createMap();
            vsscript_getVariable(_vsScript, VPS_DISCONNECT_VARIABLE_NAME, scriptOutputs);
            if (AVSF_VS_API->propNumElements(scriptOutputs, VPS_DISCONNECT_VARIABLE_NAME) == 1) {
                toDisconnect = AVSF_VS_API->propGetInt(scriptOutputs, VPS_DISCONNECT_VARIABLE_NAME, 0, nullptr) != 0;
            }
            AVSF_VS_API->freeMap(scriptOutputs);
        } else {
            _errorString = vsscript_getError(_vsScript);
        }
    }

    if (_errorString.empty()) {
        if (_scriptClip == nullptr) {
            _scriptClip = sourceClip;
        }
    } else {
        // add trailing '\n' to pad size because snprintf() does not count the terminating null
        const char *errorFormat =
"from vapoursynth import core\n\
core.text.Text(%s, r'''%s''').set_output()\n";

        const size_t errorScriptSize = snprintf(nullptr, 0, errorFormat, VPS_SOURCE_NODE_NAME, _errorString.c_str());
        const std::unique_ptr<char []> errorScript(new char[errorScriptSize]);
        snprintf(errorScript.get(), errorScriptSize, errorFormat, VPS_SOURCE_NODE_NAME, _errorString.c_str());
        if (vsscript_evaluateScript(&_vsScript, errorScript.get(), "VpsFilter_Error", efSetWorkingDir) == 0) {
            _scriptClip = vsscript_getOutput(_vsScript, 0);
        } else {
            _scriptClip = sourceClip;
        }
    }

    if (sourceClip != _scriptClip) {
        AVSF_VS_API->freeNode(sourceClip);
    }

    if (toDisconnect && !ignoreDisconnect) {
        return false;
    }

    Environment::GetInstance().Log(L"New script clip: %p", _scriptClip);
    _scriptVideoInfo = *AVSF_VS_API->getVideoInfo(_scriptClip);
    _scriptAvgFrameDuration = llMulDiv(_scriptVideoInfo.fpsDen, UNITS, _scriptVideoInfo.fpsNum, 0);

    return true;
}

MainScriptInstance::~MainScriptInstance() {
    AVSF_VS_API->freeFrame(_sourceDrainFrame);
}

auto MainScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from main instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        _sourceAvgFrameRate = static_cast<int>(llMulDiv(FrameServer::GetInstance()._sourceVideoInfo.fpsNum, FRAME_RATE_SCALE_FACTOR, _scriptVideoInfo.fpsDen, 0));
        _sourceAvgFrameDuration = llMulDiv(FrameServer::GetInstance()._sourceVideoInfo.fpsDen, UNITS, FrameServer::GetInstance()._sourceVideoInfo.fpsNum, 0);

        AVSF_VS_API->freeFrame(_sourceDrainFrame);
        _sourceDrainFrame = AVSF_VS_API->newVideoFrame(FrameServer::GetInstance()._sourceVideoInfo.format,
                                                       FrameServer::GetInstance()._sourceVideoInfo.width,
                                                       FrameServer::GetInstance()._sourceVideoInfo.height,
                                                       nullptr, vsscript_getCore(_vsScript));

        return true;
    }

    return false;
}

auto MainScriptInstance::LinkFrameHandler(FrameHandler *frameHandler) -> void {
    this->_frameHandler = frameHandler;
}

auto MainScriptInstance::GetErrorString() const -> std::optional<std::string> {
    return _errorString.empty() ? std::nullopt : std::make_optional(_errorString);
}

auto CheckingScriptInstance::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from checking instance");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        StopScript();

        return true;
    }

    return false;
}

/**
 * Create media type based on a template while changing its subtype. Also change fields in format if necessary.
 *
 * For example, when the original subtype has 8-bit samples and new subtype has 16-bit,
 * all "size" and FourCC values will be adjusted.
 */
auto CheckingScriptInstance::GenerateMediaType(const Format::PixelFormat &pixelFormat, const AM_MEDIA_TYPE *templateMediaType) const -> CMediaType {
    FOURCCMap fourCC(&pixelFormat.mediaSubtype);

    CMediaType newMediaType(*templateMediaType);
    newMediaType.SetSubtype(&pixelFormat.mediaSubtype);

    VIDEOINFOHEADER *newVih = reinterpret_cast<VIDEOINFOHEADER *>(newMediaType.Format());
    BITMAPINFOHEADER *newBmi;

    if (SUCCEEDED(CheckVideoInfo2Type(&newMediaType))) {
        VIDEOINFOHEADER2 *newVih2 = reinterpret_cast<VIDEOINFOHEADER2 *>(newMediaType.Format());
        newBmi = &newVih2->bmiHeader;

        // generate new DAR if the new SAR differs from the old one
        // because AviSynth does not tell us anything about DAR, scaled the DAR wit the ratio between new SAR and old SAR
        if (_scriptVideoInfo.width * abs(newBmi->biHeight) != _scriptVideoInfo.height * newBmi->biWidth) {
            const long long ax = static_cast<long long>(newVih2->dwPictAspectRatioX) * _scriptVideoInfo.width * std::abs(newBmi->biHeight);
            const long long ay = static_cast<long long>(newVih2->dwPictAspectRatioY) * _scriptVideoInfo.height * newBmi->biWidth;
            const long long gcd = std::gcd(ax, ay);
            newVih2->dwPictAspectRatioX = static_cast<DWORD>(ax / gcd);
            newVih2->dwPictAspectRatioY = static_cast<DWORD>(ay / gcd);
        }
    } else {
        newBmi = &newVih->bmiHeader;
    }

    newVih->rcSource = { .left = 0, .top = 0, .right = _scriptVideoInfo.width, .bottom = _scriptVideoInfo.height };
    newVih->rcTarget = newVih->rcSource;
    newVih->AvgTimePerFrame = llMulDiv(_scriptVideoInfo.fpsDen, UNITS, _scriptVideoInfo.fpsNum, 0);

    newBmi->biWidth = _scriptVideoInfo.width;
    newBmi->biHeight = _scriptVideoInfo.height;
    newBmi->biBitCount = pixelFormat.bitCount;
    newBmi->biSizeImage = GetBitmapSize(newBmi);
    newMediaType.SetSampleSize(newBmi->biSizeImage);

    if (fourCC == pixelFormat.mediaSubtype) {
        // uncompressed formats (such as RGB32) have different GUIDs
        newBmi->biCompression = fourCC.GetFOURCC();
    } else {
        newBmi->biCompression = BI_RGB;
    }

    return newMediaType;
}

FrameServer::FrameServer() {
    Environment::GetInstance().Log(L"FrameServer()");
    Environment::GetInstance().Log(L"Filter version: %S", FILTER_VERSION_STRING);

    const int vsInitCounter = vsscript_init();
    ASSERT(vsInitCounter == 1);

    _vsApi = vsscript_getVSApi2(VAPOURSYNTH_API_VERSION);
    VSCore *vsCore = _vsApi->createCore(0);

    VSCoreInfo coreInfo;
    _vsApi->getCoreInfo2(vsCore, &coreInfo);

    _versionString
        .append(" R").append(std::to_string(coreInfo.core))
        .append(" API R").append(std::to_string(VAPOURSYNTH_API_MAJOR)).append(".").append(std::to_string(VAPOURSYNTH_API_MINOR));
    Environment::GetInstance().Log(L"VapourSynth version: %S", GetVersionString());

    _vsApi->freeCore(vsCore);
}

FrameServer::~FrameServer() {
    Environment::GetInstance().Log(L"~FrameServer()");

    vsscript_finalize();
}

auto FrameServer::SetScriptPath(const std::filesystem::path &scriptPath) -> void {
    _scriptPath = scriptPath;
}

auto FrameServer::GetVersionString() const -> const char * {
    return _versionString.c_str();
}

}
