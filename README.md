# [AviSynth Filter](https://github.com/CrendKing/avisynth_filter)

A DirectShow filter that puts AviSynth into video playing, by loading AviSynth script and feed the generated frames to video player.

This filter exports an "AvsFilterSource()" function to the AviSynth script, which serves as a source plugin. This filter feeds the video samples from DirectShow upstream to the script. Then it sends the processed frame data to the downstream.

If you used ffdshow's AviSynth plugin, you may find this filter similar in many ways. On top of that, this filter is actively adding new features.

## Features

* Support wide range of input formats:
    * 4:2:0: NV12, YV12, P010, P016, etc
    * 4:2:2: YUY2, P210, P216
    * 4:4:4: YV24
    * RGB24, RGB32
* High performance multithreaded frame delivery
* HDR metadata passthrough
* [API and Remote Control](#api-and-remote-control)
* [Portable mode](https://github.com/CrendKing/avisynth_filter/wiki/Portable-mode)

## Requirement

* CPU with SSE2 instruction set.
* [AviSynth+](https://github.com/AviSynth/AviSynthPlus) 3.5.1 (interface version 7) and above.

## Install

* Before anything, install AviSynth+. Make sure `AviSynth.dll` is reachable either in the directory of the video player, system directories or directories from the `PATH` environment variable.
* Unpack the archive.
* Run install.bat to register the filter `avisynth_filter.ax`.
* Enable the filter `AviSynth Filter` in video player.

## Uninstall

Run uninstall.bat to unregister the filter and clean up user data.

## Usage

The filter exports the following functions to the AviSynth script.

#### `AvsFilterSource()`

The source function which returns a `clip` object. Similar to other source functions like `AviSource()`.

This function takes no argument.

#### `AvsFilterDisconnect()`

This function serves as a heuristic to disconnect the AviSynth Filter from DirectShow filter graph. Put at the end of the script file.

It can be used to avoid unnecessary processing and improve performance if the script does not modify the source. Avoid to use it during live reloading.

A good example is if your script applies modifications based on video metadata (e.g. FPS < 30), without using this function, even if the condition does not hold the filter still needs to copy every frame. At best, it wastes both CPU and memory resource for nothing. At worst, it breaks hardware acceleration chain for certain filters. For instance, when [LAV Filters](https://github.com/Nevcairiel/LAVFilters) connects directly to [madVR](http://www.madvr.com/) in D3D11 mode, the GPU decoded frames are not copied to memory. If any filter goes between them, the frame needs to be copied.

This function takes no argument.

## API and Remote Control

Since version 0.6.0, this filter allows other programs to remotely control it via API. By default the functionality is disabled and can be activated from settings (requires restarting the video player after changing).

For details of the API, please refer to the comments in source file [api.h](https://github.com/CrendKing/avisynth_filter/blob/master/avisynth_filter/src/api.h).

## Example AviSynth script

Add a line of text to videos with less than 20 FPS. Otherwise disconnect the filter.

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

## Build

A build script `build.bat` is included to automate the process. It obtains dependencies and starts compilation. Before running `build.bat`, make sure you have the latest Visual Studio installed. The script will automatically assume the Visual Studio developer environment and check out dependencies. To run the script, pass the target platform as the first argument, e.g. `build.bat x64` or `build.bat x86`.

## Credit

Thanks to [Milardo from Doom9's Forum](https://forum.doom9.org/member.php?u=159393) for help initially testing the project.

Thanks to [chainikdn from SVP team](https://github.com/chainikdn) for contributing features.
