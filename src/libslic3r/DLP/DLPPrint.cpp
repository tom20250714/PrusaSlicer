///|/ DLP print engine scaffold.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DLPPrint.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>
#include <tbb/parallel_for.h>

#include "DLPDirectoryArchive.hpp"
#ifndef SLIC3R_DLP_CORE_ONLY
#include "DLPGeometrySlicer.hpp"
#endif
#include "DLPRasterizer.hpp"

namespace Slic3r::dlp {

namespace {

void report(const ProgressFn &progress, double value, const char *message)
{
    if (progress)
        progress(value, message);
}

void check_cancel(const CancelFn &cancel)
{
    if (cancel)
        cancel();
}

void prepare_layers(std::vector<Layer> &layers, const PrinterConfig &printer,
                    const ProcessConfig &process, const ExecutionConfig &execution)
{
    if (execution.placement == PlacementMode::CenterOnBuildArea)
        center_layers_on_build_area(layers, printer);
    for (size_t index = 0; index < layers.size(); ++index) {
        layers[index].index = static_cast<uint32_t>(index);
        layers[index].exposure_time_s = index < process.initial_layer_count ?
            process.initial_exposure_time_s : process.exposure_time_s;
    }
}

void rasterize_and_write(std::vector<Layer> &layers, const PrinterConfig &printer,
                         const ProcessConfig &process, const std::string &output_path,
                         const ExecutionConfig &execution, const Rasterizer &rasterizer,
                         const OpticalCorrector &corrector, ArchiveWriter &writer,
                         const CancelFn &cancel, const ProgressFn &progress)
{
    writer.begin_output(printer, process, layers.size(), output_path);
    const size_t width = printer.resolution.width_px;
    const size_t height = printer.resolution.height_px;
    if (height != 0 && width > std::numeric_limits<size_t>::max() / height)
        throw std::overflow_error("DLP raster dimensions exceed the addressable buffer size");
    const size_t expected_pixels = width * height;
    const size_t requested_workers = execution.max_parallel_layers == 0 ?
        static_cast<size_t>(std::thread::hardware_concurrency()) : execution.max_parallel_layers;
    const size_t worker_count = std::max<size_t>(1, std::min<size_t>(8, requested_workers));

    for (size_t batch_begin = 0; batch_begin < layers.size(); batch_begin += worker_count) {
        check_cancel(cancel);
        const size_t batch_end = std::min(layers.size(), batch_begin + worker_count);
        std::vector<RasterLayer> rasters(batch_end - batch_begin);
        tbb::parallel_for(batch_begin, batch_end, [&](size_t index) {
            rasters[index - batch_begin] = rasterizer.rasterize(layers[index], printer, cancel);
        });
        for (size_t index = batch_begin; index < batch_end; ++index) {
            RasterLayer &raster = rasters[index - batch_begin];
            raster.index = layers[index].index;
            raster.height_mm = layers[index].height_mm;
            raster.exposure_time_s = layers[index].exposure_time_s;
            raster.resolution = printer.resolution;
            corrector.correct(raster, printer, cancel);
            if (raster.grayscale.size() != expected_pixels)
                throw std::runtime_error("DLP rasterizer returned a buffer with an invalid size");
            writer.add_layer(std::move(raster));
        }
        const double fraction = layers.empty() ? 1.0 : static_cast<double>(batch_end) / layers.size();
        report(progress, 0.4 + 0.55 * fraction, "Rasterizing DLP layers");
    }
    check_cancel(cancel);
    report(progress, 0.97, "Writing DLP output");
    writer.finish(output_path);
}

} // namespace

void center_layers_on_build_area(std::vector<Layer> &layers, const PrinterConfig &printer)
{
    coord_t min_x = std::numeric_limits<coord_t>::max();
    coord_t min_y = std::numeric_limits<coord_t>::max();
    coord_t max_x = std::numeric_limits<coord_t>::lowest();
    coord_t max_y = std::numeric_limits<coord_t>::lowest();
    bool has_points = false;
    for (const Layer &layer : layers) {
        for (const ExPolygons *polygons : {&layer.model, &layer.supports}) {
            for (const ExPolygon &polygon : *polygons) {
                for (const Point &point : polygon.contour.points) {
                    min_x = std::min(min_x, point.x());
                    min_y = std::min(min_y, point.y());
                    max_x = std::max(max_x, point.x());
                    max_y = std::max(max_y, point.y());
                    has_points = true;
                }
            }
        }
    }
    if (! has_points)
        return;

    const Point target = Point::new_scale(printer.build_width_mm * 0.5,
                                           printer.build_height_mm * 0.5);
    const Point offset(target.x() - (min_x + max_x) / 2,
                       target.y() - (min_y + max_y) / 2);
    for (Layer &layer : layers) {
        for (ExPolygons *polygons : {&layer.model, &layer.supports}) {
            for (ExPolygon &polygon : *polygons) {
                for (Point &point : polygon.contour.points)
                    point += offset;
                for (Polygon &hole : polygon.holes)
                    for (Point &point : hole.points)
                        point += offset;
            }
        }
    }
}

Print::Print(const GeometrySlicer &slicer, const SupportGenerator &support_generator,
             const Rasterizer &rasterizer, const OpticalCorrector &optical_corrector,
             ArchiveWriter &writer)
    : m_slicer(slicer)
    , m_support_generator(support_generator)
    , m_rasterizer(rasterizer)
    , m_optical_corrector(optical_corrector)
    , m_writer(writer)
{}

std::string Print::validate(const PrinterConfig &printer, const ProcessConfig &process)
{
    if (printer.resolution.width_px == 0 || printer.resolution.height_px == 0)
        return "DLP projector resolution must be greater than zero";
    if (! std::isfinite(printer.build_width_mm) || ! std::isfinite(printer.build_height_mm) ||
        printer.build_width_mm <= 0.0 || printer.build_height_mm <= 0.0)
        return "DLP build dimensions must be finite and greater than zero";
    if (printer.antialiasing_samples == 0 || printer.antialiasing_samples > 8)
        return "DLP anti-aliasing samples must be between 1 and 8";
    if (! std::isfinite(process.layer_height_mm) || process.layer_height_mm <= 0.0)
        return "DLP layer height must be finite and greater than zero";
    if (! std::isfinite(process.exposure_time_s) || process.exposure_time_s < 0.0 ||
        ! std::isfinite(process.initial_exposure_time_s) || process.initial_exposure_time_s < 0.0)
        return "DLP exposure times must be finite and non-negative";
    return {};
}

void Print::process(const Model &model, const PrinterConfig &printer, const ProcessConfig &process,
                    const std::string &output_path, CancelFn cancel, ProgressFn progress,
                    const ExecutionConfig &execution) const
{
    if (const std::string error = validate(printer, process); ! error.empty())
        throw std::invalid_argument(error);
    if (output_path.empty())
        throw std::invalid_argument("DLP output path must not be empty");

    check_cancel(cancel);
    report(progress, 0.0, "Slicing DLP geometry");
    std::vector<Layer> layers = m_slicer.slice(model, printer, process, cancel);

    check_cancel(cancel);
    report(progress, 0.35, "Generating DLP supports");
    m_support_generator.generate(model, layers, printer, process, cancel);

    prepare_layers(layers, printer, process, execution);
    rasterize_and_write(layers, printer, process, output_path, execution, m_rasterizer,
                        m_optical_corrector, m_writer, cancel, progress);
    report(progress, 1.0, "DLP processing complete");
}

namespace {

class NoSupports final : public SupportGenerator
{
public:
    void generate(const Model &, std::vector<Layer> &, const PrinterConfig &,
                  const ProcessConfig &, const CancelFn &) const override {}
};

class NoOpticalCorrection final : public OpticalCorrector
{
public:
    void correct(RasterLayer &, const PrinterConfig &, const CancelFn &) const override {}
};

} // namespace

#ifndef SLIC3R_DLP_CORE_ONLY
ExportResult export_directory(const Model &model, const PrinterConfig &printer,
                              const ProcessConfig &process, const std::string &output_path,
                              const ExecutionConfig &execution, CancelFn cancel,
                              ProgressFn progress)
{
    FixedLayerGeometrySlicer slicer;
    NoSupports supports;
    GrayscaleRasterizer rasterizer;
    NoOpticalCorrection correction;
    DirectoryArchiveWriter writer(execution.output);
    Print print(slicer, supports, rasterizer, correction, writer);
    print.process(model, printer, process, output_path, std::move(cancel), std::move(progress), execution);
    return {writer.layer_count()};
}
#endif

ExportResult export_layers(std::vector<Layer> layers, const PrinterConfig &printer,
                           const ProcessConfig &process, const std::string &output_path,
                           const ExecutionConfig &execution, CancelFn cancel,
                           ProgressFn progress)
{
    if (const std::string error = Print::validate(printer, process); ! error.empty())
        throw std::invalid_argument(error);
    if (output_path.empty())
        throw std::invalid_argument("DLP output path must not be empty");
    GrayscaleRasterizer rasterizer;
    NoOpticalCorrection correction;
    DirectoryArchiveWriter writer(execution.output);
    prepare_layers(layers, printer, process, execution);
    rasterize_and_write(layers, printer, process, output_path, execution, rasterizer,
                        correction, writer, cancel, progress);
    report(progress, 1.0, "DLP processing complete");
    return {writer.layer_count()};
}

} // namespace Slic3r::dlp
