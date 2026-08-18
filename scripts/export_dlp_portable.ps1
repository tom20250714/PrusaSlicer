param(
    [string]$Destination = "$PSScriptRoot\..\build\dlp-portable-package"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null

$items = @(
    "src\libslic3r\DLP",
    "src\libslic3r\PNGReadWrite.cpp",
    "src\libslic3r\PNGReadWrite.hpp",
    "src\dlp-slicer.cpp",
    "cmake\dlp_core\CMakeLists.txt",
    "tests\dlp_print\CMakeLists.txt",
    "tests\dlp_print\dlp_print_tests.cpp",
    "tests\dlp_print\benchmark_dlp.ps1",
    "docs\DLP_BAMBU_PORTING.md"
)

foreach ($relative in $items) {
    $source = Join-Path $root $relative
    $target = Join-Path $destinationPath $relative
    $parent = Split-Path -Parent $target
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination $parent -Recurse -Force
    } else {
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
}

@"
DLP portable source package

Start with docs/DLP_BAMBU_PORTING.md.
Merge PNGReadWrite changes; do not blindly replace the target host file.
Do not copy Prusa-specific src/libslic3r/DLPPrint.* during the initial port.
"@ | Set-Content -LiteralPath (Join-Path $destinationPath "README.txt") -Encoding UTF8

Write-Host "DLP portable package created at: $destinationPath"
