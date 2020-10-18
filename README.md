# [AviSynth Filter](https://github.com/CrendKing/avisynth_filter)

A DirectShow filter that loads an AviSynth script and feed the frames to the video player.

This filter exports an "AvsFilterSource()" function to the AviSynth script, which serves as a source plugin. This filter feeds the video samples from DirectShow upstream to the script. Then it sends the processed frame data to the downstream.

If you used ffdshow's AviSynth plugin, you may find this filter similar in many ways. On top of that, this filter is actively adding new features. Support most common input formats such as NV12, YUY2 and P010 etc.

## Requirement

* CPU with SSSE3 instructions.
* [AviSynth+](https://github.com/AviSynth/AviSynthPlus) 3.5.1 (interface version 7) and above.

## Install

* Before anything, install AviSynth+. Make sure `AviSynth.dll` is reachable either in the directory of the video player, system directories or directories from the `PATH` environment variable.
* Unpack the archive.
* Run install.bat to register the filter `avisynth_filter.ax`.
* Enable the filter `AviSynth Filter` in video player.

## Uninstall

Run uninstall.bat to unregister the filter and clean up user data.

## API

The filter exports the following functions to the AviSynth script.

#### `AvsFilterSource()`

The source function which returns a `clip` object. Similar to other source functions like `AviSource()`.

This function takes no argument.

#### `AvsFilterDisconnect()`

This function serves as a heuristic to disconnect the AviSynth Filter from DirectShow filter graph. Put at the end of the script file.

It can be used to avoid unnecessary processing and improve performance if the script does not modify the source. Avoid to use it during live reloading.

A good example is if your script applies modifications based on video metadata (e.g. FPS < 30), without using this function, even if the condition does not hold the filter still needs to copy every frame. At best, it wastes both CPU and memory resource for nothing. At worst, it breaks hardware acceleration chain for certain filters. For instance, when [LAV Filters](https://github.com/Nevcairiel/LAVFilters) connects directly to [madVR](http://www.madvr.com/) in D3D11 mode, the GPU decoded frames are not copied to memory. If any filter goes between them, the frame needs to be copied.

This function takes no argument.

## Note

* Similar to ffdshow, there are two frame buffers: ahead buffer and back buffer. The filter will wait until AviSynth to fill the buffer before starting to deliver frames.
* Unlike ffdshow, this filter automatic calibrates for optimal buffer sizes at the beginning of playback.
* The optimal buffer sizes depends on the performance of the computer hardware as well as the AviSynth script. If any change is made to to the script during playback, user could use the "Reload" button in the settings page to trigger recalibration.
* Use the input format selectors if user wants to only activate the filter on certain video formats.

## API and Remote Control

Since version 0.6.0, this filter allows other programs to remotely control it via API. By default the functionality is disabled and can be activated by importing **activate_remote_control.reg** file.

For details of the API, please refer to the comments in source file [api.h](https://github.com/CrendKing/avisynth_filter/blob/master/avisynth_filter/src/api.h).

## Example script

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

A build script `build.bat` is included to automate the process. It obtains dependencies and starts compilation. The project statically links to the DirectShow filter base classes from https://github.com/microsoft/Windows-classic-samples, thus requiring building it from source. Microsoft has not updated the sample for long time, and the sample solution is still on Visual Studio 2005. One needs to upgrade the solution before building it (already included in `build.bat`).

Before running `build.bat`, make sure you are in an development environment, such as Visual Studio developer command prompt. To run the script, pass the target platform as the first argument, e.g. `build.bat x64` or `build.bat x86`.

## Credit

Thanks to [Milardo from Doom9's Forum](https://forum.doom9.org/member.php?u=159393) for help initially testing the project.

Thanks to [chainikdn from SVP team](https://github.com/chainikdn) for contributing features.
