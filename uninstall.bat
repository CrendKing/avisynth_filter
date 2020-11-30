regsvr32 /u "%~dp0avisynth_filter_64.ax"
regsvr32 /u "%~dp0avisynth_filter_32.ax"
start /min reg delete HKEY_CURRENT_USER\Software\AviSynthFilter /f
