param([switch]$Demo,[string]$HostAddress='192.168.10.50')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$projectRoot=Split-Path -Parent $PSScriptRoot
$scriptPath=Join-Path $projectRoot 'PC_Tools\Qt_Gateway_Manager\main.py'
$pythonExe=Get-GatewayPython
& $pythonExe -c 'from PyQt5 import QtWidgets'
if ($LASTEXITCODE -ne 0) {throw 'PyQt5 is not available in this Python environment. Install PC_Tools/Qt_Gateway_Manager/requirements.txt.'}
$logDir=Join-Path $projectRoot 'build\PC_Manager'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$kind=if ($Demo) {'demo'} else {'board'}
$pidFile=Join-Path $logDir "$kind.pid"
if (Test-Path -LiteralPath $pidFile) {
    $priorId=[int](Get-Content -LiteralPath $pidFile)
    $prior=Get-CimInstance Win32_Process -Filter "ProcessId=$priorId" -ErrorAction SilentlyContinue
    if ($prior -and $prior.CommandLine -and $prior.CommandLine.IndexOf($scriptPath,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        Write-Host "Gateway Manager ($kind) is already open. Select its window on the taskbar."
        exit 0
    }
}
$pythonWindowed=Join-Path (Split-Path -Parent $pythonExe) 'pythonw.exe'
if (!(Test-Path -LiteralPath $pythonWindowed)) {$pythonWindowed=$pythonExe}
$arguments=@(('"'+$scriptPath+'"'),'--host',$HostAddress)
if ($Demo) {$arguments+='--demo'}
$stamp=Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$log=Join-Path $logDir "$kind-$stamp.log"
$errorLog=Join-Path $logDir "$kind-$stamp-error.log"
# This is the user-requested interactive GUI; pythonw does not create a console.
$process=Start-Process -FilePath $pythonWindowed -ArgumentList $arguments -WorkingDirectory $projectRoot -WindowStyle Normal -PassThru -RedirectStandardOutput $log -RedirectStandardError $errorLog
Start-Sleep -Milliseconds 900
$process.Refresh()
if ($process.HasExited) {
    if (Test-Path -LiteralPath $errorLog) {Get-Content -LiteralPath $errorLog}
    throw 'Gateway Manager exited during startup. Inspect build/PC_Manager logs.'
}
Set-Content -LiteralPath $pidFile -Value $process.Id
Write-Host "Gateway Manager opened ($kind), PID $($process.Id)."
if (!$Demo) {Write-Host "Target: $HostAddress. Click Connect in the application after configuring Ethernet. No motor command was sent."}
