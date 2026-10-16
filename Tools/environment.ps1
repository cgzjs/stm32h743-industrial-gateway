# Machine-specific overrides belong in local.env.ps1 (excluded from Git/export).
$localEnvironment=Join-Path $PSScriptRoot 'local.env.ps1'
if (Test-Path -LiteralPath $localEnvironment) {. $localEnvironment}

function Get-GatewayPython {
    $root=Split-Path -Parent $PSScriptRoot
    $candidates=@($env:IGW_PYTHON,(Join-Path $root '.venv\Scripts\python.exe'))
    $onPath=Get-Command python.exe -ErrorAction SilentlyContinue
    if ($onPath) {$candidates+=$onPath.Source}
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {return $candidate}
    }
    throw 'Python not found. Install Python 3.10+ or set IGW_PYTHON.'
}

function Get-GatewayCubeIDE([string]$Requested='') {
    if ($Requested) {return $Requested}
    if ($env:CUBE_IDE_DIR) {return $env:CUBE_IDE_DIR}
    $found=Get-ChildItem -Path 'C:\ST\STM32CubeIDE_*\STM32CubeIDE' -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($found) {return $found.FullName}
    throw 'Set CUBE_IDE_DIR to the STM32CubeIDE installation directory, or pass -CubeIDE.'
}

function Get-GatewayArmBin {
    if ($env:ARM_GCC_DIR) {return $env:ARM_GCC_DIR}
    $bundled=Get-ChildItem "$env:LOCALAPPDATA\stm32cube\bundles\gnu-tools-for-stm32\*\bin\arm-none-eabi-gdb.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($bundled) {return $bundled.DirectoryName}
    $onPath=Get-Command arm-none-eabi-gdb.exe -ErrorAction SilentlyContinue
    if ($onPath) {return Split-Path -Parent $onPath.Source}
    throw 'ARM tools not found. Add their bin directory to PATH or set ARM_GCC_DIR.'
}
