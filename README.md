# AviSynth Filter

A DirectShow filter that loads an AviSynth script and feed the frames to the video player.

This filter exports an "avsfilter_source()" function to the AviSynth script, which serves as a source plugin. This filter feeds the video samples from DirectShow upstream to the script. Then it sends the processed frame data to the downstream.

If you used ffdshow's AviSynth plugin, you may find this filter similar in many ways. On top of that, this filter is actively adding new features. Support most common input formats such as NV12, YUY2 and P010 etc.

## Install

* Before anything, install [AviSynth+](https://github.com/AviSynth/AviSynthPlus/). Make sure `AviSynth.dll` is either in system directories or at the same directory of this filter.
* Unpack the archive.
* Run install.bat to register the filter `avisynth_filter.ax`.
* Enable the filter `AviSynth Filter` in video player.

## Uninstall

Run uninstall.bat to unregister the filter and clean up user data.

## Interface

The filter exports the following functions to the AviSynth script.

#### `avsfilter_source()`

The source function which returns a `clip` object. Similar to other source functions like `AviSource()`.

This function takes no argument.

#### `avsfilter_disconnect()`

This function serves as a heuristic to disconnect the AviSynth Filter from DirectShow filter graph. Put at the end of the script file.

It can be used to avoid unnecessary processing and improve performance if the script does not modify the source. Avoid to use it during live reloading.

This function takes no argument.

## Example script

Add a line of text to videos with less than 20 FPS. Otherwise disconnect the filter.

```
avsfilter_source()

fps = Round(FrameRate())
if (fps < 20) {
    Subtitle("This video has low FPS")
    Prefetch(4)
} else {
    avsfilter_disconnect()
}
```

## Build

A build script `build.bat` is included to automate the process. It obtains dependencies and triggers compiling. The project depends on the DirectShow filter base classes from https://github.com/microsoft/Windows-classic-samples. Microsoft has not updated the sample for long time, and the sample solution is still on Visual Studio 2005. One needs to upgrade the solution before building it.
