# AviSynth Filter

A DirectShow filter that loads an AviSynth script and feed the frames to the video player.

This filter exports an "avsfilter_source()" function to the avs script, which serves as an AviSynth source plugin. This function feeds the video samples from the upstream DirectShow filter to the script. Then it sends the processed frame data to the downstream. Basically this filter does what ffdshow's AviSynth plugin do, while being an bare minimum independent filter. Currently support NV12, YV12, P010 and P016 4:2:0 input formats (up from ffdshow's YV12 only).

Because the filter has different internal implementation and logic, you may find some problems you experienced with ffdshow is gone. If you find any new problem, please file an issue.

## Install

* Unpack
* Run install.bat to register the filter `avisynth_filter.ax`
* Enable the filter `AviSynth Filter` in video player

## Uninstall

* Run uninstall.bat to unregister the filter and clean up user data

## Build

A build script `build.bat` is included to automate the process. It obtains dependencies and triggers compiling. The project depends on the DirectShow filter base classes from https://github.com/microsoft/Windows-classic-samples. Microsoft has not updated the sample for long time, and the sample solution is still on Visual Studio 2005. One needs to upgrade the solution before building it.
