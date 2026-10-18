[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Port = '',
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)][string[]]$IdfArguments
)
$ErrorActionPreference = 'Stop'
$env:PYTHONUTF8 = '1'
$env:IDF_PATH = 'C:\esp\v6.1\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.1\venv'
$env:ESP_IDF_VERSION = '6.1'
$env:IDF_CCACHE_ENABLE = '0'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\esp-rom-elfs\20241011'
$toolEnvLines = & 'C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1' -e 6>$null
$toolPathLine = $toolEnvLines | Where-Object { $_ -like 'PATH=*' } | Select-Object -First 1
if (-not $toolPathLine) { throw 'ESP-IDF tool PATH was not found' }
$env:PATH = $toolPathLine.Substring(5) + ';' + $env:PATH
# idf.py resolves substituted drives back to their Unicode paths. Build a
# synchronized copy in an ASCII-only temporary directory instead.
$stagePath = Join-Path $env:TEMP 'ComputerScreen-esp-idf-v6.1'
New-Item -ItemType Directory -Path (Join-Path $stagePath 'main') -Force | Out-Null
foreach ($sourceName in @('CMakeLists.txt', 'sdkconfig.defaults', 'partitions.csv')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $sourceName) -Destination $stagePath -Force
}
if (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'dependencies.lock')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'dependencies.lock') -Destination $stagePath -Force
}
Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'main') -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $stagePath 'main') -Force
}
Write-Host "Build directory: $stagePath"
Push-Location $stagePath
try {
    if ($Port) { $IdfArguments = @('-p', $Port) + $IdfArguments }
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" @IdfArguments
    $idfExitCode = $LASTEXITCODE
    if ($idfExitCode -eq 0 -and (Test-Path -LiteralPath (Join-Path $stagePath 'build\project_description.json'))) {
        & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" (Join-Path $PSScriptRoot 'tools\export_ota.py') --build-dir (Join-Path $stagePath 'build') --output-dir (Join-Path $PSScriptRoot 'release')
        if ($LASTEXITCODE -ne 0) { throw 'OTA export failed' }
    }
    if (Test-Path -LiteralPath (Join-Path $stagePath 'dependencies.lock')) {
        Copy-Item -LiteralPath (Join-Path $stagePath 'dependencies.lock') -Destination $PSScriptRoot -Force
    }
} finally {
    Pop-Location
}
exit $idfExitCode
