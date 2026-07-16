///|/ DLP print engine scaffold.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DLPPrint_hpp_
#define slic3r_DLPPrint_hpp_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "libslic3r/ExPolygon.hpp"

namespace Slic3r { class Model; }

namespace Slic3r::dlp {

struct Resolution
{
    uint32_t width_px  { 0 };
    uint32_t height_px { 0 };
};

struct PrinterConfig
{
    Resolution resolution;
    double     build_width_mm  { 0.0 };
    double     build_height_mm { 0.0 };
    bool       mirror_x        { false };
    bool       mirror_y        { false };
    uint8_t    antialiasing_samples { 1 };
};

struct ProcessConfig
{
    double layer_height_mm        { 0.05 };
    double exposure_time_s        { 2.0 };
    double initial_exposure_time_s{ 20.0 };
    uint32_t initial_layer_count  { 5 };
};

enum class PlacementMode
{
    PreserveModelPosition,
    CenterOnBuildArea
};

struct OutputConfig
{
    std::string layer_prefix { "SEC_" };
    std::string layer_extension { ".png" };
    uint32_t    first_layer_number { 1 };
    uint8_t     minimum_number_digits { 4 };
    std::string preview_filename { "Preview_t.png" };
    bool        write_preview { true };
    bool        write_manifest { true };
    bool        write_viewer { true };
};

struct ExecutionConfig
{
    PlacementMode placement { PlacementMode::CenterOnBuildArea };
    // Zero selects the available hardware concurrency, capped internally to
    // keep full-resolution raster batches from consuming excessive memory.
    uint32_t max_parallel_layers { 0 };
    OutputConfig output;
};

struct ExportResult
{
    size_t layer_count { 0 };
};

struct Layer
{
    uint32_t   index { 0 };
    double     height_mm { 0.0 };
    ExPolygons model;
    ExPolygons supports;
    double     exposure_time_s { 0.0 };
};

struct RasterLayer
{
    uint32_t             index { 0 };
    double               height_mm { 0.0 };
    double               exposure_time_s { 0.0 };
    Resolution           resolution;
    std::vector<uint8_t> grayscale;
};

using CancelFn   = std::function<void()>;
using ProgressFn = std::function<void(double, const std::string &)>;

class GeometrySlicer
{
public:
    virtual ~GeometrySlicer() = default;
    virtual std::vector<Layer> slice(const Model &, const PrinterConfig &, const ProcessConfig &,
                                     const CancelFn &) const = 0;
};

class SupportGenerator
{
public:
    virtual ~SupportGenerator() = default;
    virtual void generate(const Model &, std::vector<Layer> &, const PrinterConfig &,
                          const ProcessConfig &, const CancelFn &) const = 0;
};

class OpticalCorrector
{
public:
    virtual ~OpticalCorrector() = default;
    virtual void correct(RasterLayer &, const PrinterConfig &, const CancelFn &) const = 0;
};

class Rasterizer
{
public:
    virtual ~Rasterizer() = default;
    virtual RasterLayer rasterize(const Layer &, const PrinterConfig &, const CancelFn &) const = 0;
};

class ArchiveWriter
{
public:
    virtual ~ArchiveWriter() = default;
    virtual void begin(const PrinterConfig &, const ProcessConfig &, size_t layer_count) = 0;
    virtual void begin_output(const PrinterConfig &printer, const ProcessConfig &process,
                              size_t layer_count, const std::string &)
        { begin(printer, process, layer_count); }
    virtual void add_layer(RasterLayer &&) = 0;
    virtual void finish(const std::string &output_path) = 0;
};

// Coordinates the DLP pipeline. Algorithms and output formats are supplied as
// independent components and deliberately have no dependency on SLAPrint.
class Print
{
public:
    Print(const GeometrySlicer &, const SupportGenerator &, const Rasterizer &,
          const OpticalCorrector &, ArchiveWriter &);

    void process(const Model &, const PrinterConfig &, const ProcessConfig &,
                 const std::string &output_path, CancelFn cancel = {}, ProgressFn progress = {},
                 const ExecutionConfig &execution = {}) const;

    static std::string validate(const PrinterConfig &, const ProcessConfig &);

private:
    const GeometrySlicer   &m_slicer;
    const SupportGenerator &m_support_generator;
    const Rasterizer       &m_rasterizer;
    const OpticalCorrector &m_optical_corrector;
    ArchiveWriter          &m_writer;
};

void center_layers_on_build_area(std::vector<Layer> &, const PrinterConfig &);

// Portable high-level entry point used by GUI and CLI adapters. It owns the
// standard geometry, raster and directory-output components so host slicers do
// not need to duplicate pipeline orchestration.
ExportResult export_directory(const Model &, const PrinterConfig &, const ProcessConfig &,
                              const std::string &output_path,
                              const ExecutionConfig &execution = {}, CancelFn cancel = {},
                              ProgressFn progress = {});

// Entry point for adapters that already produced layer polygons, such as the
// lightweight binary-STL CLI. Uses the same placement, parallel raster and
// streaming archive path as export_directory().
ExportResult export_layers(std::vector<Layer>, const PrinterConfig &, const ProcessConfig &,
                           const std::string &output_path,
                           const ExecutionConfig &execution = {}, CancelFn cancel = {},
                           ProgressFn progress = {});

} // namespace Slic3r::dlp

#endif
