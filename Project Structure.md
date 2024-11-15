The project is roughly divided into 3 pieces: the DirectShow filter for AviSynth (`avisynth_filter`), for VapourSynth (`vapoursynth_filter`) and the common logic sharing between the two (`filter_common`). The base classes of DirectShow are also included in the repository (`baseclasses`).

The frame server specific module, i.e. `avisynth_filter` and `vapoursynth_filter`, contains the logic specific to that server:

* `frameserver.cpp`: Initialization and destruction of the instance of the frame server, as well as script manipulation and frame processing.
* `frame_handler.cpp`: Translation of frames between DirectShow and the frame server, as well as the cache of the frames.
* `format.cpp`: Interpretation and manipulation of video formats specific to the frame server, as well as frame content I/O.

Note that logic of the aspects above that are common between frame servers stays in `filter_common`. For example, there is `frameserver_common.cpp` that supplements `frameserver.cpp`, `frame_handler_common.cpp` supplements `frame_handler.cpp`. `format.h` houses the SIMD functions for copying frame content from one format to another, e.g. from interleaving NV12 to individual Y, U, V planes.

The common module `filter_common` houses the rest of the project. A few notable components are:

* `environment.cpp`: Setting up logging facility. Loading and saving settings in file or registry.
* `filter.cpp`: Main file for setting up the DirectShow filter.
* `main.cpp`: Entry point of the DLL, registering DLL as well as setting up DirectShow pins.
* `prop_settings.cpp`: The "Settings" tab of the filter property window.
* `prop_status.cpp`: The "Status" tab of the filter property window.
* `remote_control.cpp`: Handling the remote control requests, i.e. the APIs. The definitions of the APIs can be found in `api.h`.

The Visual Studio project files (`*.vcxproj`, `*.vcxitems`) are all hand-crafted. They follow the same principle of reusing as much common logic as possible, similar to the project structure.

* `common.props` from the root directory: Contain the common Visual Studio project content. Both .vcxproj must import this file to form valid project file.
* `filter_common.vcxitems` from `filter_common`: Shared items for .vcxproj. Contains the files from `filter_common` and the DirectShow `baseclasses`.
* `avisynth_filter.vcxproj` and `vapoursynth_filter.vcxproj`: Main project files with their specific definitions of preprocessors and build options. Must import the two files above.
