# [AviSynth Filter and VapourSynth Filter](https://github.com/CrendKing/avisynth_filter)

DirectShow filters that put AviSynth or VapourSynth into video playing, by loading a script and feed the generated frames to video player.

Each filter takes video samples from upstream, feeds them to the frame server in compatible format, and delivers the transformed output frames to downstream.

If you used ffdshow's AviSynth plugin before, you may find these filters similar in many ways. On top of that, our filters are actively adding new features.

## Features

* Support wide range of input formats:
    * 4:2:0: NV12, YV12, P010, P016, etc
    * 4:2:2: YUY2, P210, P216
    * 4:4:4: YV24
    * RGB24, RGB32
* High performance frame generation and delivery
* HDR metadata passthrough
* [API and Remote Control](#api-and-remote-control)
* [Portable mode](https://github.com/CrendKing/avisynth_filter/wiki/Portable-mode)

## Requirement

* CPU with SSE2 instruction set.
* [AviSynth+](https://github.com/AviSynth/AviSynthPlus) 3.5.1 (interface version 7) and above, or
* [VapourSynth](https://github.com/vapoursynth/vapoursynth).

## Install

* Before anything, install AviSynth+ or VapourSynth. Make sure the frame server's dll files are reachable either in the directory of the video player, system directories or directories from the `PATH` environment variable.
* Unpack the archive.
* Run install.bat to register the filter .ax files.
* Enable `AviSynth Filter` or `VapourSynth Filter` in video player.

## Uninstall

Run uninstall.bat to unregister the filters and clean up user data.

## Usage

### AviSynth Filter

The filter exposes the following functions to the AviSynth script:

#### `AvsFilterSource()`

The source function which returns an [`IClip`](http://avisynth.nl/index.php/Filter_SDK/Cplusplus_API#IClip) object. Similar to other source functions like `AviSource()`.

This function takes no argument.

#### `AvsFilterDisconnect()`

This function serves as a heuristic to disconnect the filter itself from DirectShow filter graph. Put at the end of the script file.

It can be used to avoid unnecessary processing and improve performance if the script does not modify the source. Avoid to use it during live reloading.

A good example is if your script applies modifications based on video metadata (e.g. FPS < 30), without using this function, even if the condition does not hold the filter still needs to copy every frame. At best, it wastes both CPU and memory resource for nothing. At worst, it breaks hardware acceleration chain for certain filters. For instance, when [LAV Filters](https://github.com/Nevcairiel/LAVFilters) connects directly to [madVR](http://www.madvr.com/) in D3D11 mode, the GPU decoded frames are not copied to memory. If any filter goes between them, the frame needs to be copied.

This function takes no argument.

### VapourSynth

The filter exposes the following variables to the VapourSynth Python script:

#### `VpsFilterSource`

This variable has the type of [`VideoNode`](http://www.vapoursynth.com/doc/pythonreference.html#VideoNode). It serves as the source clip.

Note that the [`fps`](http://www.vapoursynth.com/doc/pythonreference.html#VideoNode.fps) property of the video node is set to the average framerate instead of 0 / 1, regardless if frames have variable frame durations. It is equivalent to mpv's [`container_fps`](https://mpv.io/manual/master/#video-filters-container-fps) variable.

The following [Reserved Frame Properties](http://www.vapoursynth.com/doc/apireference.html#reserved-frame-properties) are supported:

* `_FieldBased` (always 0)
* `_AbsoluteTime`
* `_DurationNum`
* `_DurationDen`
* `_SARNum`
* `_SARDen`

#### `VpsFilterDisconnect`

This variable does not exist at the entry of the script. Upon return, if this variable exists and has an non-zero value, it disconnects the filter itself from DirectShow filter graph.

## API and Remote Control

Since version 0.6.0, these filters allow other programs to remotely control it via API. By default the functionality is disabled and can be activated from settings (requires restarting the video player after changing).

For details of the API, please refer to the comments in source file [api.h](https://github.com/CrendKing/avisynth_filter/blob/master/avisynth_filter/src/api.h).

## Example scripts

Add a line of text to videos with less than 20 FPS. Otherwise disconnect the filter.

### AviSynth

```
AvsFilterSource()

fps = Round(FrameRate())
if (fps < 20) {
    Subtitle("This video has low FPS")
    Prefetch(4)
} else {
    AvsFilterDisconnect()
}
```

### VapourSynth

```
from vapoursynth import core
import math

fps = round(VpsFilterSource.fps)
if fps < 20:
    core.text.Text(VpsFilterSource, 'This video has low FPS').set_output()
else:
    VpsFilterDisconnect = True
```

## Build

A build script `build.ps1` is included to automate the process. It obtains dependencies and starts compilation. Before running `build.ps1`, make sure you have the latest Visual Studio installed. The script will automatically assume the Visual Studio developer environment and check out dependencies. To run the script, pass the target platform as the first argument, e.g. `build.ps1 x64` or `build.ps1 x86`.

## Credit

Thanks to [Milardo from Doom9's Forum](https://forum.doom9.org/member.php?u=159393) for help initially testing the project.

Thanks to [chainikdn from SVP team](https://github.com/chainikdn) for contributing features.
