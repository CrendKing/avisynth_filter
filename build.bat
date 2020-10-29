cd /d "%~dp0"

for /f "delims=" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -nologo -utf8 -latest -find **\vcvars64.bat ) do call "%%i"

if not exist dep_directshow (
    mkdir checkout_directshow
    cd checkout_directshow
    git init
    git remote add -f origin https://github.com/microsoft/Windows-classic-samples.git
    git config core.sparseCheckout true
    echo /Samples/Win7Samples/multimedia/directshow/baseclasses > .git/info/sparse-checkout
    git reset --hard origin/master
    cd ..
    move checkout_directshow\Samples\Win7Samples\multimedia\directshow\baseclasses dep_directshow
    rmdir /s /q checkout_directshow

    devenv.exe /Upgrade dep_directshow\baseclasses.sln
)

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

set ds_platform=%1
set filter_platform=%1

if "%ds_platform%" == "x86" (
    set ds_platform=Win32
)

MSBuild.exe -property:Configuration=Release;Platform=%ds_platform% -maxCpuCount -nologo dep_directshow\baseclasses.sln
MSBuild.exe -property:Configuration=Release;Platform=%filter_platform%;ForceImportAfterCppProps="%~dp0build.props" -maxCpuCount -nologo avisynth_filter.sln
