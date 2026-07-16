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
