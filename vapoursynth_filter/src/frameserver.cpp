// License: https://github.com/CrendKing/avisynth_filter/blob/master/LICENSE

#include "frameserver.h"

#include "api.h"
#include "frame_handler.h"


namespace SynthFilter {

static constexpr const char *VPS_VAR_NAME_SOURCE_NODE = "VpsFilterSource";
static constexpr const char *VPS_VAR_NAME_DISCONNECT = "VpsFilterDisconnect";

static auto VS_CC SourceGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) -> const VSFrame * {
    FrameHandler *frameHandler = reinterpret_cast<FrameHandler *>(instanceData);
    if (frameHandler == nullptr) {
        AVSF_VPS_API->setFilterError("VapourSynth Filter: Source frame is requested before the frame handler is ready", frameCtx);
        return nullptr;
    }

    return AVSF_VPS_API->addFrameRef(frameHandler->GetSourceFrame(n));
}

AutoReleaseVSFrame::AutoReleaseVSFrame(VSFrame *newFrame)
    : frame(newFrame) {}

AutoReleaseVSFrame::~AutoReleaseVSFrame() {
    Destroy();
}

auto AutoReleaseVSFrame::operator=(VSFrame *other) -> const AutoReleaseVSFrame & {
    Destroy();
    frame = other;
    return *this;
}

auto AutoReleaseVSFrame::Destroy() -> void {
    AVSF_VPS_API->freeFrame(frame);
    frame = nullptr;
}

FrameServerCommon::FrameServerCommon() {
    Environment::GetInstance().Log(L"FrameServerCommon()");

    _vsScriptApi = getVSScriptAPI(VSSCRIPT_API_VERSION);
    _vsApi = getVapourSynthAPI(VAPOURSYNTH_API_VERSION);
    if (_vsApi == nullptr || _vsScriptApi == nullptr) {
        throw "VapourSynth API 4.0 is required";
    }

    VSCore *vsCore = _vsApi->createCore(0);
    VSCoreInfo coreInfo;
    _vsApi->getCoreInfo(vsCore, &coreInfo);
    _vsApi->freeCore(vsCore);

    _versionString = std::format("VapourSynth R{} API R{}.{}", coreInfo.core, coreInfo.api >> 16, coreInfo.api & 0xffff);
    Environment::GetInstance().Log(L"VapourSynth version: %hs", GetVersionString().data());
}

auto FrameServerBase::StopScript() -> void {
    if (_scriptClip != nullptr) {
        Environment::GetInstance().Log(L"Release script clip: %p", _scriptClip);
        AVSF_VPS_API->freeNode(_scriptClip);
        _scriptClip = nullptr;
    }
}

FrameServerBase::FrameServerBase() {
    _vsScript = AVSF_VPS_SCRIPT_API->createScript(nullptr);
    _vsCore = AVSF_VPS_SCRIPT_API->getCore(_vsScript);
}

FrameServerBase::~FrameServerBase() {
    StopScript();
    AVSF_VPS_API->freeNode(_sourceClip);
    AVSF_VPS_SCRIPT_API->freeScript(_vsScript);
}

/**
 * Create new script clip with specified media type.
 */
auto FrameServerBase::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    StopScript();
    AVSF_VPS_API->freeNode(_sourceClip);

    const VSVideoInfo &sourceVideoInfo = Format::GetVideoFormat(mediaType, this).videoInfo;
    _sourceClip = AVSF_VPS_API->createVideoFilter2("VpsFilter_Source", &sourceVideoInfo, SourceGetFrame, nullptr, fmParallelRequests, nullptr, 0, _frameHandler, GetVsCore());
    AVSF_VPS_API->setCacheMode(_sourceClip, 0);

    VSMap *sourceInputs = AVSF_VPS_API->createMap();
    AVSF_VPS_API->mapSetNode(sourceInputs, VPS_VAR_NAME_SOURCE_NODE, _sourceClip, 0);
    AVSF_VPS_SCRIPT_API->setVariables(_vsScript, sourceInputs);
    AVSF_VPS_API->freeMap(sourceInputs);

    _errorString.clear();

    bool toDisconnect = false;

    if (!FrameServerCommon::GetInstance()._scriptPath.empty()) {
        const std::string utf8Filename = ConvertWideToUtf8(FrameServerCommon::GetInstance()._scriptPath.native());

        if (AVSF_VPS_SCRIPT_API->evaluateFile(_vsScript, utf8Filename.c_str()) == 0) {
            _scriptClip = AVSF_VPS_SCRIPT_API->getOutputNode(_vsScript, 0);

            VSMap *scriptOutputs = AVSF_VPS_API->createMap();
            AVSF_VPS_SCRIPT_API->getVariable(_vsScript, VPS_VAR_NAME_DISCONNECT, scriptOutputs);
            if (AVSF_VPS_API->mapNumElements(scriptOutputs, VPS_VAR_NAME_DISCONNECT) == 1) {
                toDisconnect = AVSF_VPS_API->mapGetInt(scriptOutputs, VPS_VAR_NAME_DISCONNECT, 0, nullptr) != 0;
            }
            AVSF_VPS_API->freeMap(scriptOutputs);
        } else {
            _errorString = AVSF_VPS_SCRIPT_API->getError(_vsScript);
        }
    }

    if (_errorString.empty()) {
        if (_scriptClip == nullptr) {
            _scriptClip = _sourceClip;
            AVSF_VPS_API->addNodeRef(_sourceClip);
        }
    } else {
        const std::string errorScript = std::format("from vapoursynth import core\n\
core.text.Text({}, r'''{}''').set_output()",
                                                    VPS_VAR_NAME_SOURCE_NODE,
                                                    _errorString);
        if (AVSF_VPS_SCRIPT_API->evaluateBuffer(_vsScript, errorScript.c_str(), "VpsFilter_Error") == 0) {
            _scriptClip = AVSF_VPS_SCRIPT_API->getOutputNode(_vsScript, 0);
        } else {
            _scriptClip = _sourceClip;
            AVSF_VPS_API->addNodeRef(_sourceClip);
        }
    }

    if (toDisconnect && !ignoreDisconnect) {
        return false;
    }

    Environment::GetInstance().Log(L"New script clip: %p", _scriptClip);
    const VSVideoInfo *scriptVideoInfo = AVSF_VPS_API->getVideoInfo(_scriptClip);
    _scriptAvgFrameDuration = llMulDiv(scriptVideoInfo->fpsDen, UNITS, scriptVideoInfo->fpsNum, 0);

    return true;
}

auto MainFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from main frameserver");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        const VSVideoInfo *sourceVideoInfo = AVSF_VPS_API->getVideoInfo(_sourceClip);
        _sourceAvgFrameRate = static_cast<int>(llMulDiv(sourceVideoInfo->fpsNum, FRAME_RATE_SCALE_FACTOR, sourceVideoInfo->fpsDen, 0));
        _sourceAvgFrameDuration = llMulDiv(sourceVideoInfo->fpsDen, UNITS, sourceVideoInfo->fpsNum, 0);
        return true;
    }

    return false;
}

auto MainFrameServer::CreateSourceDummyFrame() const -> const VSFrame * {
    const VSVideoInfo *sourceVideoInfo = AVSF_VPS_API->getVideoInfo(_sourceClip);
    return AVSF_VPS_API->newVideoFrame(&sourceVideoInfo->format, sourceVideoInfo->width, sourceVideoInfo->height, nullptr, GetVsCore());
}

auto AuxFrameServer::ReloadScript(const AM_MEDIA_TYPE &mediaType, bool ignoreDisconnect) -> bool {
    Environment::GetInstance().Log(L"ReloadScript from auxiliary frameserver");

    if (__super::ReloadScript(mediaType, ignoreDisconnect)) {
        _scriptVideoInfo = *AVSF_VPS_API->getVideoInfo(_scriptClip);
        StopScript();

        return true;
    }

    return false;
}

auto MainFrameServer::LinkFrameHandler(FrameHandler *frameHandler) -> void {
    _frameHandler = frameHandler;
}

auto AuxFrameServer::GetScriptPixelType() const -> uint32_t {
    return AVSF_VPS_API->queryVideoFormatID(_scriptVideoInfo.format.colorFamily,
                                            _scriptVideoInfo.format.sampleType,
                                            _scriptVideoInfo.format.bitsPerSample,
                                            _scriptVideoInfo.format.subSamplingW,
                                            _scriptVideoInfo.format.subSamplingH,
                                            GetVsCore());
}

}
