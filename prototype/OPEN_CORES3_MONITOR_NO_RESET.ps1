param([string]$Port = 'COM10')
$pythonPath = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $pythonPath)) { $pythonPath = 'python' }
$monitorPath = Join-Path $PSScriptRoot 'tools\monitor_no_reset.py'
& $pythonPath $monitorPath --port $Port
exit $LASTEXITCODE
