if "%VSINSTALLDIR%"=="" for /f "delims=" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -nologo -utf8 -version [16.9, -latest -find **\vcvars64.bat') do call "%%i"
if "%VSINSTALLDIR%"=="" (
    echo "Unable to activate Visual Studio environment"
    exit /b 1
)

cd /d "%~dp0"

if not exist dep_avisynth_plus (
    mkdir checkout_avisynth_plus
    cd checkout_avisynth_plus
    git init
    git remote add -f origin https://github.com/AviSynth/AviSynthPlus.git
    git config core.sparseCheckout true
    echo /avs_core/include > .git/info/sparse-checkout
    git reset --hard origin/master
    cd ..
    move checkout_avisynth_plus\avs_core\include dep_avisynth_plus
    rmdir /s /q checkout_avisynth_plus
)

if not exist dep_simpleini (
    git clone https://github.com/brofield/simpleini.git dep_simpleini
)

set configuration=%1
set platform=%2

MSBuild.exe -property:Configuration=%configuration%;Platform=%platform% -maxCpuCount -nologo avisynth_filter.sln
