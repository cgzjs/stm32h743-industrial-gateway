param([switch]$Stop,[string]$CubeIDE='',[int]$Speed=1000)
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$projectRoot=Split-Path -Parent $PSScriptRoot
$pidFile=Join-Path $projectRoot 'build\openocd.pid'
if ($Stop) {
    if (Test-Path -LiteralPath $pidFile) {$debugPid=[int](Get-Content -LiteralPath $pidFile);$debugProcess=Get-Process -Id $debugPid -ErrorAction SilentlyContinue;if ($debugProcess -and $debugProcess.ProcessName -eq 'openocd') {Stop-Process -Id $debugPid};Remove-Item -LiteralPath $pidFile}
    exit
}
if (Test-Path -LiteralPath $pidFile) {$debugProcess=Get-Process -Id ([int](Get-Content -LiteralPath $pidFile)) -ErrorAction SilentlyContinue;if ($debugProcess -and $debugProcess.ProcessName -eq 'openocd') {exit}}
$CubeIDE=Get-GatewayCubeIDE $CubeIDE
$exe=(Get-ChildItem -Path "$CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.win32_*\tools\bin\openocd.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$scriptCandidates=@((Join-Path (Split-Path -Parent (Split-Path -Parent $exe)) 'share\openocd\scripts'))
$scriptCandidates+=@(Get-ChildItem "$CubeIDE\plugins\com.st.stm32cube.ide.mcu.debug.openocd_*\resources\openocd\st_scripts" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -ExpandProperty FullName)
$scripts=$scriptCandidates | Where-Object {Test-Path -LiteralPath (Join-Path $_ 'target\stm32h7x.cfg')} | Select-Object -First 1
if (!$scripts) {throw 'OpenOCD target scripts not found in the CubeIDE installation'}
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'build') -Force | Out-Null
$arguments=@('-s',('"'+$scripts+'"'),'-f','interface/stlink-dap.cfg','-c','"transport select dapdirect_swd"','-c','"set AP_NUM 0"','-c','"set GDB_PORT 3333"','-c','"set CORE_RESET 0"','-c','"set STOP_WATCHDOG 1"','-c','"set DUAL_BANK 1"','-c',('"set CLOCK_FREQ '+$Speed+'"'),'-f','target/stm32h7x.cfg','-c',('"adapter speed '+$Speed+'"'),'-c','"reset_config none"')
$process=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Hidden -PassThru -RedirectStandardOutput "$projectRoot\build\openocd.log" -RedirectStandardError "$projectRoot\build\openocd-error.log"
Set-Content -LiteralPath $pidFile -Value $process.Id
$ready=$false
for ($attempt=0;$attempt -lt 50;$attempt++) {
    $process.Refresh()
    if ($process.HasExited) {throw 'OpenOCD exited. Read build/openocd-error.log'}
    $probe=New-Object System.Net.Sockets.TcpClient
    try {$pending=$probe.BeginConnect('127.0.0.1',3333,$null,$null);if ($pending.AsyncWaitHandle.WaitOne(100)) {$probe.EndConnect($pending);$ready=$true}} catch {} finally {$probe.Dispose()}
    if ($ready) {break}
    Start-Sleep -Milliseconds 100
}
if (!$ready) {throw 'OpenOCD GDB port 3333 did not become ready'}
