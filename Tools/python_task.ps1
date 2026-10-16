param([Parameter(Mandatory=$true)][string]$Script,[string[]]$ScriptArguments=@())
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$pythonExe=Get-GatewayPython
Push-Location (Split-Path -Parent $PSScriptRoot)
try {
    & $pythonExe $Script @ScriptArguments
    if ($LASTEXITCODE -ne 0) {throw "Python task failed: $Script"}
} finally {Pop-Location}
