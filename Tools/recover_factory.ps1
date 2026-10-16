param([switch]$RecoverJournal,[ValidateSet('Debug','Release')][string]$Mode='Debug')
$ErrorActionPreference='Stop'
. "$PSScriptRoot\environment.ps1"
if (!$RecoverJournal) {throw 'Use -RecoverJournal to replace the application and clear the two OTA boot journals. Slot images and device configuration are preserved.'}
$projectRoot=Split-Path -Parent $PSScriptRoot
$boot=Join-Path $projectRoot "build\$Mode\Bootloader.elf"
$app=(Join-Path $projectRoot "build\$Mode\Gateway.bin").Replace('\','/')
if (!(Test-Path -LiteralPath $boot) -or !(Test-Path -LiteralPath $app)) {throw 'Build both firmware images first'}
$gdb=Join-Path (Get-GatewayArmBin) 'arm-none-eabi-gdb.exe'
& "$PSScriptRoot\debug_server.ps1"
$commands=@"
set pagination off
set confirm off
target remote localhost:3333
monitor reset halt
tbreak boot_factory_gate
continue
monitor flash write_image erase "$app" 0x08020000 bin
set {unsigned int} &boot_recovery_request = 0x52454333
tbreak jump_vectors
continue
monitor resume
detach
quit
"@
$scriptFile=Join-Path $projectRoot 'build\recover.gdb'
Set-Content -LiteralPath $scriptFile -Value $commands -Encoding ascii
try { & $gdb --batch -x $scriptFile $boot; if ($LASTEXITCODE -ne 0) {throw 'Recovery failed; inspect target state before retrying'} } finally { & "$PSScriptRoot\debug_server.ps1" -Stop }
