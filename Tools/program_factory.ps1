param([ValidateSet('Debug','Release')][string]$Mode='Release',[string]$CubeIDE='',[ValidateSet('Software','Hardware')][string]$ResetMode='Software',[switch]$CheckOnly)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$CubeIDE=Get-GatewayCubeIDE $CubeIDE
$projectRoot=Split-Path -Parent $PSScriptRoot
$cli=(Get-ChildItem -Path "$CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
if (!$cli -or !(Test-Path -LiteralPath $cli)) {throw 'STM32CubeProgrammer CLI not found. Specify -CubeIDE with the local installation directory.'}
$boot=Join-Path $projectRoot "build\$Mode\Bootloader.bin"
$app=Join-Path $projectRoot "build\$Mode\Gateway.bin"
if (!(Test-Path -LiteralPath $boot) -or !(Test-Path -LiteralPath $app)) {throw 'Build both firmware images first'}
$manifestPath=Join-Path $projectRoot "build\$Mode\Gateway.json"
if (!(Test-Path -LiteralPath $manifestPath)) {throw 'Gateway.json is missing; run Tools/build.ps1 first'}
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$appFile=Get-Item -LiteralPath $app
$appHash=(Get-FileHash -LiteralPath $app -Algorithm SHA256).Hash
if ($manifest.format -ne 'IGW3-manifest-v1' -or $manifest.board -ne 0x743A0001 -or $manifest.base -ne 0x08020000 -or $manifest.size -ne $appFile.Length -or $manifest.sha256 -ne $appHash) {throw 'Application identity or SHA256 does not match Gateway.json'}
if ((Get-Item -LiteralPath $boot).Length -gt 0x20000 -or $appFile.Length -gt 0x1E0000) {throw 'Firmware exceeds its Flash partition'}
# For first provisioning, when NOR does not contain a valid IGW3 boot journal.
# Later SWD replacement requires factory recovery as explained in Docs/BRINGUP.md.
Write-Host 'First provisioning: writing Bootloader at 0x08000000, then Gateway at 0x08020000.'
$connectionArgs=@('-c','port=SWD','mode=NORMAL','reset=SWrst','freq=1000')
if ($ResetMode -eq 'Hardware') {
    $connectionArgs=@('-c','port=SWD','mode=UR','reset=HWrst','freq=1000')
    Write-Host 'Hardware reset selected: NRST must be connected.'
} else {
    Write-Host 'Software reset selected: NRST is optional. Keep SWDIO, SWCLK, GND connected and target powered.'
}
if ($CheckOnly) {
    Write-Host ('Validated connection options: '+($connectionArgs -join ' '))
    Write-Host 'Firmware identity, SHA256 and partition sizes passed. Check only: no target connection or write performed.'
    exit 0
}
& $cli @connectionArgs -d $boot 0x08000000 -v
if ($LASTEXITCODE -ne 0) {throw 'Bootloader programming failed'}
& $cli @connectionArgs -d $app 0x08020000 -v -rst
if ($LASTEXITCODE -ne 0) {throw 'Application programming failed'}
Write-Host 'Factory programming complete: both images verified and target reset.'
