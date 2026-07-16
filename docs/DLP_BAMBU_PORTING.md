# DLP engine porting guide for Bambu Studio

This package ports the independent DLP engine, not PrusaSlicer's SLA engine or
the experimental `DLPPrint` background-process adapter.

## 1. Files to copy

Copy the complete `src/libslic3r/DLP` directory into the Bambu Studio tree:

- `DLPPrint.*`: portable pipeline API and configuration
- `DLPGeometrySlicer.*`: Bambu/Prusa `Model` to layer polygons adapter
- `DLPRasterizer.*`: scanline mask rasterizer
- `DLPDirectoryArchive.*`: streaming PNG directory writer
- `DLPBinarySTL.*`: optional standalone CLI input adapter

Also merge, rather than blindly replace, these host files:

- `src/libslic3r/PNGReadWrite.hpp`
- `src/libslic3r/PNGReadWrite.cpp`

The required host change is the `write_gray_to_file_fast()` declaration and
implementation. It selects PNG compression level 1 and disables PNG filters.

Do not initially copy these PrusaSlicer-specific integrations:

- `src/libslic3r/DLPPrint.*` (outside the `DLP` directory)
- `SLIC3R_DLP_ONLY` startup/preset changes
- DLP additions to `BackgroundSlicingProcess`
- Prusa configuration-wizard workarounds

## 2. libslic3r CMake integration

Add these sources to Bambu Studio's libslic3r source list:

```cmake
DLP/DLPPrint.cpp
DLP/DLPPrint.hpp
DLP/DLPGeometrySlicer.cpp
DLP/DLPGeometrySlicer.hpp
DLP/DLPRasterizer.cpp
DLP/DLPRasterizer.hpp
DLP/DLPDirectoryArchive.cpp
DLP/DLPDirectoryArchive.hpp
```

`DLPBinarySTL.*` is only required for the standalone CLI. The integrated
engine requires TBB, Boost filesystem/nowide, libpng and nlohmann-json. These
are normally already present in PrusaSlicer-derived applications, but target
names and include paths must be checked in the selected Bambu Studio revision.

## 3. First GUI adapter

Add one menu action only. Do not switch the whole application to SLA/DLP
printer technology during the first port.

The host adapter needs a stable snapshot of the current model and calls:

```cpp
Slic3r::dlp::PrinterConfig printer {{1920, 1080}, 120.0, 67.5};
Slic3r::dlp::ProcessConfig process;
process.layer_height_mm = 0.05;

Slic3r::dlp::ExecutionConfig execution;
execution.placement = Slic3r::dlp::PlacementMode::CenterOnBuildArea;

const Slic3r::dlp::ExportResult result = Slic3r::dlp::export_directory(
    model_snapshot, printer, process, output_path, execution, cancel, progress);
```

For controllers that accept layer PNGs only:

```cpp
execution.output.write_preview = false;
execution.output.write_manifest = false;
execution.output.write_viewer = false;
```

Stop or synchronize Bambu Studio's background slicing worker before taking the
model snapshot. Resume it after export. The Prusa integration originally hit a
model-revision assertion when the live model and background worker were read at
the same time.

## 4. Host API compatibility points

Resolve compilation differences only at these boundaries:

1. `Model::mesh()` in `DLPGeometrySlicer.cpp`.
2. `slice_mesh_ex()` and `TriangleMeshSlicer.hpp` names/signatures.
3. `ExPolygon`, `Polygon`, `Point`, `scale_()` and `unscale()` namespaces.
4. `png::write_gray_to_file_fast()` host merge.
5. Bambu GUI menu and file-dialog APIs.

Keep `DLPRasterizer`, `DLPDirectoryArchive` and the public structures unchanged
where possible. This preserves golden-test comparability between both hosts.

## 5. Required validation order

1. Build and run `dlp_print_tests` without GUI integration.
2. Confirm all 13 test cases and 77 assertions pass.
3. Run the same STL through the Prusa and Bambu builds.
4. Compare layer count, manifest values and decoded PNG pixels.
5. Test preserve-position and center-position modes.
6. Test X/Y mirror, holes, supports, multiple objects and transformed instances.
7. Run `benchmark_dlp.ps1` with the same model and machine.
8. Verify a small asymmetric model on the physical DLP controller.

The physical test must confirm white/black exposure polarity, X/Y direction,
layer numbering, preview requirements and whether extra JSON/HTML files are
accepted by the controller.

## 6. Baseline from this repository

- Tests: 13 cases, 77 assertions
- Baseline model: `20x20x20.stl`
- Resolution: 1920 x 1080
- Layer height: 0.05 mm
- Layers: 400
- Observed benchmark: approximately 2.6 seconds on the development machine

Do not use the timing as a strict unit-test threshold. Record it as a regression
metric because CPU, storage and build type materially affect it.

## 7. Licensing

PrusaSlicer and this DLP implementation use AGPL-compatible source headers.
Preserve copyright/license notices and verify the target Bambu Studio fork's
license and distribution obligations before shipping binaries.
