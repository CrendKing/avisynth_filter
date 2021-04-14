cd /d "%~dp0"

del AviSynthFilter_Debug.zip

cd x64\Debug\
7z a ..\..\AviSynthFilter_Debug.zip avisynth_filter_64.ax avisynth_filter_64.pdb baseclasses_64.pdb
7z rn ..\..\AviSynthFilter_Debug.zip avisynth_filter_64.ax x64\avisynth_filter_64.ax avisynth_filter_64.pdb x64\avisynth_filter_64.pdb baseclasses_64.pdb x64\baseclasses_64.pdb

cd ..\..\Win32\Debug\
7z a ..\..\AviSynthFilter_Debug.zip avisynth_filter_32.ax avisynth_filter_32.pdb baseclasses_32.pdb
7z rn ..\..\AviSynthFilter_Debug.zip avisynth_filter_32.ax Win32\avisynth_filter_32.ax avisynth_filter_32.pdb Win32\avisynth_filter_32.pdb baseclasses_32.pdb Win32\baseclasses_32.pdb
