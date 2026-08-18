param(
    [Parameter(Mandatory = $true)][string]$Slicer,
    [Parameter(Mandatory = $true)][string]$Model,
    [string]$OutputRoot = "$PSScriptRoot\benchmark-output",
    [string]$Resolution = "1920x1080",
    [string]$BuildArea = "120x67.5",
    [double]$LayerHeight = 0.05
)

$ErrorActionPreference = "Stop"
$slicerPath = (Resolve-Path -LiteralPath $Slicer).Path
$modelPath = (Resolve-Path -LiteralPath $Model).Path
$output = Join-Path $OutputRoot ("run-" + [DateTime]::Now.ToString("yyyyMMdd-HHmmss") + ".dlp")
New-Item -ItemType Directory -Path $OutputRoot -Force | Out-Null

$timer = [Diagnostics.Stopwatch]::StartNew()
& $slicerPath $modelPath $output `
    --resolution $Resolution `
    --build-area $BuildArea `
    --layer-height $LayerHeight
$exitCode = $LASTEXITCODE
$timer.Stop()
if ($exitCode -ne 0) {
    throw "DLP benchmark failed with exit code $exitCode"
}

$layers = @(Get-ChildItem -LiteralPath $output -Filter "SEC_*.png")
$bytes = (Get-ChildItem -LiteralPath $output -File | Measure-Object -Property Length -Sum).Sum
[pscustomobject]@{
    Model = $modelPath
    Resolution = $Resolution
    BuildArea = $BuildArea
    LayerHeightMm = $LayerHeight
    LayerCount = $layers.Count
    ElapsedSeconds = [math]::Round($timer.Elapsed.TotalSeconds, 3)
    OutputMegabytes = [math]::Round($bytes / 1MB, 3)
    Output = $output
} | Format-List
