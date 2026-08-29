param(
    [string]$Version = "1.0.1",
    [string]$HostIp = "192.168.1.3",
    [int]$Port = 8070,
    [string]$Firmware = ""
)

$ProjectDir = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Firmware)) {
    $Firmware = Join-Path $ProjectDir "build\EVT_ESP32-S3.bin"
}

$BundledPython = "C:\Users\zhoucan\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if (Test-Path -LiteralPath $BundledPython) {
    $PythonExe = $BundledPython
} else {
    $PythonExe = (Get-Command python -ErrorAction Stop).Source
}

& $PythonExe (Join-Path $PSScriptRoot "ota_server.py") `
    --host-ip $HostIp `
    --port $Port `
    --version $Version `
    --firmware $Firmware
