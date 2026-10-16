param([string]$CubeIDE='')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
$CubeIDE=Get-GatewayCubeIDE $CubeIDE
$projectRoot=Split-Path -Parent $PSScriptRoot
$mx=(Get-ChildItem -Path "$CubeIDE\plugins\com.st.stm32cube.common.mx_*\STM32CubeMX.jar" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
$java=(Get-ChildItem -Path "$CubeIDE\plugins\com.st.stm32cube.ide.jre.win64_*\jre\bin\java.exe" | Sort-Object FullName -Descending | Select-Object -First 1).FullName
Push-Location $projectRoot
try {
    $generated=Join-Path $projectRoot 'build/CubeMX'
    New-Item -ItemType Directory -Path $generated -Force | Out-Null
    $profile=Join-Path $generated 'Industrial_Gateway.ioc'
    $cubePath=(Join-Path $projectRoot 'Vendor/STM32CubeH7').Replace('\','/')
    $ioc=Get-Content -LiteralPath (Join-Path $projectRoot 'Industrial_Gateway.ioc') -Raw
    $ioc=[regex]::Replace($ioc,'(?m)^ProjectManager.CustomerFirmwarePackage=.*$',('ProjectManager.CustomerFirmwarePackage='+$cubePath))
    Set-Content -LiteralPath $profile -Value $ioc -Encoding ascii
    $commands=@(
        ('config load "'+$profile.Replace('\','/')+'"')
        'project name CubeGenerated'
        ('project path "'+$projectRoot.Replace('\','/')+'/CubeGenerated"')
        'project toolchain STM32CubeIDE'
        ('project setCustomFWPath "'+$cubePath+'"')
        'project generate'
        'exit'
    )
    $scriptFile=Join-Path $generated 'generate.script'
    Set-Content -LiteralPath $scriptFile -Value $commands -Encoding ascii
    & $java -jar $mx -q $scriptFile
    if ($LASTEXITCODE -ne 0) {throw 'CubeMX generation failed'}
    foreach ($name in @('usart','eth','fdcan','fmc','quadspi','sdmmc')) {
        if (!(Test-Path -LiteralPath "$projectRoot\CubeGenerated\Core\Src\$name.c")) {throw "Missing generated peripheral source: $name.c"}
    }
    Write-Output 'Cube peripheral sources generated. Build the complete firmware using root CMake, not the generated IDE project.'
} finally {Pop-Location}
