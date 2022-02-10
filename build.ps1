$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -utf8 -property installationPath -version '[17,99]'
if (!$vsPath) {
    exit 1
}

Import-Module "${vsPath}\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vsPath -DevCmdArguments "-arch=amd64 -host_arch=amd64 -no_logo" -SkipAutomaticLocation

$workingDir = "${PSScriptRoot}\dep_avisynth_plus"
if (-Not (Test-Path $workingDir)) {
    mkdir $workingDir
    Set-Location $workingDir

    git clone 'https://github.com/AviSynth/AviSynthPlus.git' --sparse --depth=1 --filter=blob:none
    Set-Location AviSynthPlus
    git sparse-checkout set 'avs_core/include'
    Move-Item 'avs_core/include' '../include'
    Set-Location $workingDir

    $releases = Invoke-RestMethod -Headers @{ 'Accept' = 'application/vnd.github.v3+json' } 'https://api.github.com/repos/AviSynth/AviSynthPlus/releases'
    $foundAsset = $false
    foreach ($rel in $releases) {
        foreach ($asset in $rel.assets) {
            if ($asset.name -like 'AviSynthPlus_*-filesonly.7z') {
                Invoke-WebRequest $asset.browser_download_url -OutFile $asset.name
                7z e $asset.name -olibs\32 '*\x86\c_api\*.lib'
                7z e $asset.name -olibs\64 '*\x64\c_api\*.lib'
                $foundAsset = $true
                break
            }
        }

        if ($foundAsset) {
            break
        }
    }

    Remove-Item -Recurse -Force 'AviSynthPlus', '*.7z'
}

$workingDir = "${PSScriptRoot}\dep_vapoursynth"
if (-Not (Test-Path $workingDir)) {
    mkdir $workingDir
    Set-Location $workingDir

    $latestRelease = Invoke-RestMethod -Headers @{ 'Accept' = 'application/vnd.github.v3+json' } 'https://api.github.com/repos/vapoursynth/vapoursynth/releases'
    $foundAsset = $false
    foreach ($rel in $releases) {
        foreach ($asset in $latestRelease.assets) {
            if ($asset.name -like 'VapourSynth64-Portable-R*.7z') {
                Invoke-WebRequest $asset.browser_download_url -OutFile $asset.name
                7z e $asset.name -oinclude 'sdk\include\*'
                7z e $asset.name -olibs\32 'sdk\lib32\*'
                7z e $asset.name -olibs\64 'sdk\lib64\*'
                $foundAsset = $true
                break
            }
        }

        if ($foundAsset) {
            break
        }
    }

    Remove-Item '*.7z'
}

Set-Location $PSScriptRoot

$workingDir = "${PSScriptRoot}\dep_simpleini"
if (-Not (Test-Path $workingDir)) {
    git clone https://github.com/brofield/simpleini.git --depth=1 dep_simpleini
}

$configuration = $args[0]
$platform = $args[1]

MSBuild.exe -property:"Configuration=${configuration};Platform=${platform}" -maxCpuCount avisynth_filter.sln
exit $LASTEXITCODE
