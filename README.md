# AviSynth Filter

A DirectShow filter that loads an AviSynth script and feed the frames to the video player.

This filter exports an "avsfilter_source()" function to the avs script, which serves as an AviSynth source plugin. This function feeds the video samples from the upstream DirectShow filter to the script. Then it sends the processed frame data to the downstream. Basically this filter does what ffdshow's AviSynth plugin do, while being an bare minimum independent filter. Support most common input formats such as NV12, YUY2 and P010 etc.

Because the filter has different internal implementation and logic, you may find some problems you experienced with ffdshow is gone. If you find any new problem, please file an issue.

## Install

* Before anything, install [AviSynth+](https://github.com/AviSynth/AviSynthPlus/). Make sure `AviSynth.dll` is either in system directories or at the same directory of this filter.
* Unpack the archive.
* Run install.bat to register the filter `avisynth_filter.ax`.
* Enable the filter `AviSynth Filter` in video player.

## Uninstall

* Run uninstall.bat to unregister the filter and clean up user data

## Build

A build script `build.bat` is included to automate the process. It obtains dependencies and triggers compiling. The project depends on the DirectShow filter base classes from https://github.com/microsoft/Windows-classic-samples. Microsoft has not updated the sample for long time, and the sample solution is still on Visual Studio 2005. One needs to upgrade the solution before building it.

## Example script

Add a line of text to the video:

```
avsfilter_source()

Subtitle("Hello World")

Prefetch(4)
```
