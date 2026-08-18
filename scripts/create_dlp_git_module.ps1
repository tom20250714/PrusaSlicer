param(
    [string]$Destination = "$PSScriptRoot\..\dlp-engine-module"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..").Path
$destinationPath = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null

$copyMap = @{
    "src\libslic3r\DLP" = "src\libslic3r\DLP"
    "tests\dlp_print\dlp_print_tests.cpp" = "tests\dlp_print\dlp_print_tests.cpp"
    "tests\dlp_print\benchmark_dlp.ps1" = "tests\dlp_print\benchmark_dlp.ps1"
    "docs\DLP_BAMBU_PORTING.md" = "docs\DLP_BAMBU_PORTING.md"
    "LICENSE" = "LICENSE"
}
foreach ($entry in $copyMap.GetEnumerator()) {
    $source = Join-Path $root $entry.Key
    $target = Join-Path $destinationPath $entry.Value
    $parent = Split-Path -Parent $target
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    if (Test-Path -LiteralPath $source -PathType Container) {
        Copy-Item -LiteralPath $source -Destination $parent -Recurse -Force
    } else {
        Copy-Item -LiteralPath $source -Destination $target -Force
    }
}

@'
cmake_minimum_required(VERSION 3.16)
project(dlp_engine_module LANGUAGES CXX)

set(DLP_ENGINE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPPrint.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPGeometrySlicer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPRasterizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/libslic3r/DLP/DLPDirectoryArchive.cpp
)

function(dlp_engine_attach HOST_TARGET HOST_SOURCE_DIR)
    if (NOT TARGET ${HOST_TARGET})
        message(FATAL_ERROR "DLP host target does not exist: ${HOST_TARGET}")
    endif ()
    target_sources(${HOST_TARGET} PRIVATE ${DLP_ENGINE_SOURCES})
    target_include_directories(${HOST_TARGET} PRIVATE
        ${HOST_SOURCE_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
    )
    target_compile_features(${HOST_TARGET} PRIVATE cxx_std_17)
    message(STATUS "Attached independent DLP engine to ${HOST_TARGET}")
endfunction()
'@ | Set-Content -LiteralPath (Join-Path $destinationPath "CMakeLists.txt") -Encoding UTF8

@'
# Independent DLP engine module

Portable DLP slicing, scanline rasterization and streaming PNG export for
PrusaSlicer-derived hosts such as Bambu Studio.

## Add to Bambu Studio

```bash
git submodule add https://github.com/YOUR_ACCOUNT/dlp-engine.git external/dlp-engine
git submodule update --init --recursive
```

After Bambu Studio creates its `libslic3r` target:

```cmake
add_subdirectory(external/dlp-engine)
dlp_engine_attach(libslic3r "${CMAKE_SOURCE_DIR}/src")
```

Merge `patches/PNGReadWrite-fast.patch` into the host before building. If the
patch does not apply to the selected Bambu revision, manually add the
`png::write_gray_to_file_fast()` declaration and implementation shown there.

Do not add the Prusa-specific outer `src/libslic3r/DLPPrint.*` adapter during
the initial port. Add one GUI export command that calls `dlp::export_directory`.

See `docs/DLP_BAMBU_PORTING.md` for the complete sequence and validation list.
'@ | Set-Content -LiteralPath (Join-Path $destinationPath "README.md") -Encoding UTF8

@'
build/
.vs/
.vscode/
*.dll
*.exe
*.pdb
*.lib
*.exp
*.map
*.dlp/
benchmark-output/
'@ | Set-Content -LiteralPath (Join-Path $destinationPath ".gitignore") -Encoding UTF8

$patchDirectory = Join-Path $destinationPath "patches"
New-Item -ItemType Directory -Path $patchDirectory -Force | Out-Null
$pngPatch = & git -C $root diff -- src/libslic3r/PNGReadWrite.cpp src/libslic3r/PNGReadWrite.hpp
if ($LASTEXITCODE -ne 0 -or -not $pngPatch) {
    throw "Could not generate PNGReadWrite host patch"
}
$pngPatch | Set-Content -LiteralPath (Join-Path $patchDirectory "PNGReadWrite-fast.patch") -Encoding UTF8

Write-Host "Git-ready DLP module created at: $destinationPath"
Write-Host "Next: cd '$destinationPath'; git init; git add .; git commit -m 'Initial DLP engine module'"
