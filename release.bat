del AviSynthFilter_x64.zip
7z a AviSynthFilter_x64.zip README.md LICENSE install.bat uninstall.bat
copy /y AviSynthFilter_x64.zip AviSynthFilter_x86.zip

cd avisynth_filter\x64\Release\
7z a ..\..\..\AviSynthFilter_x64.zip avisynth_filter.ax

cd ..\..\..\avisynth_filter\Win32\Release\
7z a ..\..\..\AviSynthFilter_x86.zip avisynth_filter.ax
