param([switch]$CheckOnly,[string]$CubeIDE='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$projectRoot=Split-Path -Parent $PSScriptRoot
$outputDir=Join-Path $projectRoot 'build\TouchRecovery'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$toolDir=Get-GatewayArmBin
$gdb=Join-Path $toolDir 'arm-none-eabi-gdb.exe'
$nm=Join-Path $toolDir 'arm-none-eabi-nm.exe'
$objcopy=Join-Path $toolDir 'arm-none-eabi-objcopy.exe'
$candidates=@()
foreach ($mode in @('Release','Debug','Upgrade','TouchFix')) {
    $folder=Join-Path $projectRoot "build\$mode"
    $binary=Join-Path $folder 'Gateway.bin'
    $elf=Join-Path $folder 'Gateway.elf'
    if ((Test-Path -LiteralPath $binary) -and (Test-Path -LiteralPath $elf)) {
        $candidates+= [pscustomobject]@{Mode=$mode;Bin=$binary;Elf=$elf;Length=(Get-Item -LiteralPath $binary).Length}
    }
}
if (!$candidates.Count) {throw 'No firmware binary and matching ELF are available'}
$selected=$candidates[0]
if (!$CheckOnly) {
    $CubeIDE=Get-GatewayCubeIDE $CubeIDE
    $pidFile=Join-Path $projectRoot 'build\openocd.pid'
    if (Test-Path -LiteralPath $pidFile) {
        $prior=Get-Process -Id ([int](Get-Content -LiteralPath $pidFile)) -ErrorAction SilentlyContinue
        if ($prior -and $prior.ProcessName -eq 'openocd') {throw 'Stop the active VS Code/OpenOCD debug session before touch recovery'}
    }
    $cli=(Get-ChildItem "$CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin\STM32_Programmer_CLI.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
    if (!$cli) {throw 'STM32CubeProgrammer CLI was not found'}
    $readSize=($candidates | Measure-Object -Property Length -Maximum).Maximum
    $dump=Join-Path $outputDir 'running-application.bin'
    & $cli -c port=SWD mode=HOTPLUG freq=1000 -u 0x08020000 $readSize $dump
    if ($LASTEXITCODE -ne 0) {throw 'Could not read target. Reconnect ST-Link, power the board and close other debugger applications.'}
    $running=[IO.File]::ReadAllBytes($dump)
    $selected=$null
    foreach ($candidate in $candidates) {
        if ($running.Length -lt $candidate.Length) {continue}
        $hasher=[Security.Cryptography.SHA256]::Create()
        try {$hash=[BitConverter]::ToString($hasher.ComputeHash($running,0,$candidate.Length)).Replace('-','')} finally {$hasher.Dispose()}
        if ($hash -eq (Get-FileHash -LiteralPath $candidate.Bin -Algorithm SHA256).Hash) {$selected=$candidate;break}
    }
    if (!$selected) {throw 'Running application does not exactly match a local BIN/ELF. No RAM or Flash was changed. Keep the original matching build files.'}
}
$elfBinary=Join-Path $outputDir 'symbols-application.bin'
& $objcopy -O binary $selected.Elf $elfBinary
if ($LASTEXITCODE -ne 0 -or (Get-FileHash -LiteralPath $elfBinary -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $selected.Bin -Algorithm SHA256).Hash) {throw 'Local ELF and BIN do not match; no RAM change was attempted'}
$symbols=& $nm --defined-only $selected.Elf
if ($LASTEXITCODE -ne 0) {throw 'Could not inspect firmware symbols'}
$configLine=$symbols | Where-Object {$_ -match '^([0-9a-fA-F]+)\s+[bBdD]\s+configuration$'} | Select-Object -First 1
$entryLine=$symbols | Where-Object {$_ -match '^([0-9a-fA-F]+)\s+[tT]\s+gateway_ui_task$'} | Select-Object -First 1
if (!$configLine -or !$entryLine) {throw 'Required firmware symbols were not found'}
$configAddress=[Convert]::ToUInt32(($configLine -split '\s+')[0],16)
$entryAddress=[Convert]::ToUInt32(($entryLine -split '\s+')[0],16)
if ($configAddress -lt 0x24000000 -or $configAddress+48 -gt 0x24080000 -or $entryAddress -lt 0x08020000 -or $entryAddress -ge 0x08020000+$selected.Length) {throw 'Unexpected symbol memory range; no RAM change was attempted'}
# gateway_config_t is statically asserted to 48 bytes; its final uint32_t is at offset 44.
$flagAddress='0x{0:X8}' -f ($configAddress+44)
$entry='0x{0:X8}' -f $entryAddress
$commands=@"
set pagination off
set confirm off
set remotetimeout 5
target remote localhost:3333
monitor reset halt
thbreak *$entry
continue
set {unsigned int}$flagAddress = 0
printf "TOUCH_CALIBRATION_REENTRY_READY\n"
monitor resume
disconnect
quit
"@
$commandFile=Join-Path $outputDir 'recalibrate.gdb'
Set-Content -LiteralPath $commandFile -Value $commands -Encoding ascii
Write-Host "Matching firmware: $($selected.Mode); RAM calibration flag: $flagAddress"
if ($CheckOnly) {Write-Host 'Check only: script generated and symbols verified; no target connection, reset or write performed.';exit 0}
$started=$false
try {
    & "$PSScriptRoot\debug_server.ps1" -CubeIDE $CubeIDE -Speed 1000
    $started=$true
    $log=Join-Path $outputDir 'gdb.log'
    $errorLog=Join-Path $outputDir 'gdb-error.log'
    $arguments=@('--batch','-x',('"'+$commandFile+'"'),('"'+$selected.Elf+'"'))
    $process=Start-Process -FilePath $gdb -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError $errorLog
    if (!$process.WaitForExit(30000)) {Stop-Process -Id $process.Id;throw 'Timed out before the UI task. Press board RESET and inspect build/TouchRecovery logs.'}
    $process.Refresh()
    Get-Content -LiteralPath $log
    if ($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -Pattern 'TOUCH_CALIBRATION_REENTRY_READY' -Quiet)) {
        Get-Content -LiteralPath $errorLog
        throw 'Touch recovery did not complete. Inspect build/TouchRecovery logs; no Flash erase/program command was used.'
    }
    Write-Host 'Calibration page requested. Press the center of each displayed cross and release. Wait a few seconds after completion for settings to save.'
} finally {
    if ($started) {& "$PSScriptRoot\debug_server.ps1" -Stop -CubeIDE $CubeIDE}
}
