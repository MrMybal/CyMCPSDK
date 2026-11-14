param(
    [string]$CyUISource = (Join-Path $PSScriptRoot '../../CyImgui_Interface'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build-chat-native'),
    [string]$PortableDirectory = (Join-Path $PSScriptRoot '../out/ChatLab-windows-x64'),
    [switch]$Run
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$cyuiRoot = (Resolve-Path -LiteralPath $CyUISource).Path
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
$portableRoot = [IO.Path]::GetFullPath($PortableDirectory)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2022 with Desktop development with C++.' }
$visualStudio = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $visualStudio) { throw 'Visual Studio 2022 C++ tools were not found.' }
$env:VSLANG = '1033'
& (Join-Path $visualStudio 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmake = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja = Join-Path $visualStudio 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$temporaryFolder = Join-Path $buildRoot 'temp'
New-Item -ItemType Directory -Path $temporaryFolder -Force | Out-Null
$env:TEMP = $env:TMP = $temporaryFolder
$ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
function Invoke-Native([string]$Program, [string[]]$NativeArguments) {
    & $Program @NativeArguments
    if ($LASTEXITCODE -ne 0) { throw "Native command failed with exit code $LASTEXITCODE." }
}
Invoke-Native $cmake @('-S', $projectRoot, '-B', $buildRoot, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_MAKE_PROGRAM=$ninja", '-DCYMCP_BUILD_CHAT_APP=ON', "-DCYMCP_CYUI_SOURCE_DIR=$cyuiRoot")
# Some localized MSVC installations emit UTF-8 while CMake decodes the include
# prefix with the OEM code page. Ninja then misses header dependencies entirely.
$compilerInfo = Get-ChildItem -Path (Join-Path $buildRoot 'CMakeFiles/*/CMakeCXXCompiler.cmake') |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($compilerInfo) {
    $compilerText = [IO.File]::ReadAllText($compilerInfo.FullName)
    $prefixMatch = [regex]::Match($compilerText, 'set\(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "([^"]*)"\)')
    if ($prefixMatch.Success) {
        $prefix = $prefixMatch.Groups[1].Value
        $correctedPrefix = $prefix
        try {
            $oem = [Text.Encoding]::GetEncoding([Globalization.CultureInfo]::CurrentCulture.TextInfo.OEMCodePage)
            $strictUtf8 = New-Object Text.UTF8Encoding($false, $true)
            $correctedPrefix = $strictUtf8.GetString($oem.GetBytes($prefix))
        } catch { $correctedPrefix = $prefix }
        if ($correctedPrefix -ne $prefix) {
            $rules = Join-Path $temporaryFolder 'utf8-msvc-includes.cmake'
            $escapedPrefix = $correctedPrefix.Replace('\', '\\').Replace('"', '\"')
            $rulesText = "set(CMAKE_CL_SHOWINCLUDES_PREFIX `"$escapedPrefix`")`nset(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX `"$escapedPrefix`")`n"
            $changed = -not (Test-Path -LiteralPath $rules) -or [IO.File]::ReadAllText($rules) -cne $rulesText
            if ($changed) {
                [IO.File]::WriteAllText($rules, $rulesText, (New-Object Text.UTF8Encoding($false)))
                Invoke-Native $cmake @('-S', $projectRoot, '-B', $buildRoot, "-DCMAKE_USER_MAKE_RULES_OVERRIDE=$rules")
                # Existing objects may have been built without their header dependencies.
                Invoke-Native $cmake @('--build', $buildRoot, '--target', 'clean')
            }
        }
    }
}
Invoke-Native $cmake @('--build', $buildRoot, '--config', 'Release', '--parallel', '4')
Invoke-Native $ctest @('--test-dir', $buildRoot, '-C', 'Release', '--output-on-failure')
$installRoot = Join-Path $buildRoot 'install'
Invoke-Native $cmake @('--install', $buildRoot, '--config', 'Release', '--prefix', $installRoot)
New-Item -ItemType Directory -Path $portableRoot -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $installRoot 'bin/cymcp_chat_app.exe') -Destination $portableRoot -Force
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE'), (Join-Path $projectRoot 'THIRD_PARTY_NOTICES.md'),
    (Join-Path $projectRoot 'examples/chat_app/README.txt') -Destination $portableRoot -Force
Copy-Item -LiteralPath (Join-Path $installRoot 'share/CyMCPSDK/licenses') -Destination $portableRoot -Recurse -Force
$configFolder = Join-Path $portableRoot 'configs'
New-Item -ItemType Directory -Path $configFolder -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $projectRoot 'examples') -Filter 'chat-*.json' |
    Copy-Item -Destination $configFolder -Force
Write-Host "Application: $(Join-Path $portableRoot 'cymcp_chat_app.exe')"
if ($Run) { & (Join-Path $portableRoot 'cymcp_chat_app.exe') }
