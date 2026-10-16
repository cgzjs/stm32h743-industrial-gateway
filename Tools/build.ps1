param([ValidateSet('Debug','Release','Upgrade','Failure')][string]$Mode='Debug',[string]$Version='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$projectRoot=Split-Path -Parent $PSScriptRoot
Push-Location $projectRoot
try {
    if (!$Version) {if ($Mode -eq 'Failure') {$Version='0x00030002'} elseif ($Mode -eq 'Upgrade') {$Version='0x00030001'} else {$Version='0x00030000'}}
    $cmakeExe=(Get-Command cmake.exe -ErrorAction SilentlyContinue).Source
    if (!$cmakeExe) {$cmakeExe='C:\Program Files\CMake\bin\cmake.exe'}
    $ninjaExe=(Get-ChildItem -Path "$env:LOCALAPPDATA\stm32cube\bundles\ninja\*\bin\ninja.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1).FullName
    if (!$ninjaExe) {$ninjaExe=(Get-Command ninja.exe).Source}
    & $cmakeExe --preset $Mode "-DCMAKE_MAKE_PROGRAM=$ninjaExe" "-DIGW_VERSION=$Version"
    if ($LASTEXITCODE -ne 0) {throw 'CMake configure failed'}
    & $cmakeExe --build --preset $Mode --parallel 8
    if ($LASTEXITCODE -ne 0) {throw 'Firmware build failed'}
    $pythonExe=Get-GatewayPython
    & $pythonExe Tools/firmware.py package "build/$Mode/Gateway.bin" --version $Version
    if ($LASTEXITCODE -ne 0) {throw 'Firmware packaging failed'}
} finally {Pop-Location}
