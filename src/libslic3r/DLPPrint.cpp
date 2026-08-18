///|/ PrusaSlicer PrintBase adapter for the independent DLP engine.
#include "DLPPrint.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "Model.hpp"
#include "DLP/DLPDirectoryArchive.hpp"
#include "DLP/DLPGeometrySlicer.hpp"
#include "DLP/DLPRasterizer.hpp"

namespace Slic3r {

namespace {

template<class T> const T *config_option(const DynamicPrintConfig &config, const char *key)
{
    return dynamic_cast<const T *>(config.option(key));
}

} // namespace

void DLPPrint::clear()
{
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    m_model.clear_objects();
    m_raster_layers.clear();
    m_finished = false;
    ++m_raster_revision;
}

bool DLPPrint::empty() const
{
    return m_model.objects.empty();
}

std::vector<ObjectID> DLPPrint::print_object_ids() const
{
    std::vector<ObjectID> ids;
    ids.reserve(m_model.objects.size());
    for (const ModelObject *object : m_model.objects)
        ids.emplace_back(object->id());
    return ids;
}

std::string DLPPrint::validate(std::vector<std::string> *) const
{
    if (m_model.objects.empty())
        return "DLP print has no model objects";
    return dlp::Print::validate(m_printer_config, m_process_config);
}

PrintBase::ApplyStatus DLPPrint::apply(const Model &model, DynamicPrintConfig config,
                                      std::vector<std::string> *, const DynamicPrintConfig *)
{
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    const bool had_results = m_finished || !m_raster_layers.empty();
    // No-op while idle; stops the worker safely if configuration changes during slicing.
    this->call_cancel_callback();

    // During initial GUI startup the plater publishes an empty, transient model
    // while it is still converting the FFF workspace to DLP. There is no DLP
    // work to synchronize yet, and copying that transient per-bed state enters
    // PrusaSlicer's legacy LoadPrintData vector path. Defer the snapshot until
    // an actual object is loaded.
    if (model.objects.empty()) {
        m_model.clear_objects();
        m_raster_layers.clear();
        m_finished = false;
        ++m_raster_revision;
        return had_results ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED;
    }

    // DLP consumes object geometry only. Model::operator= also copies FFF-only
    // per-bed wipe-tower/custom-G-code vectors; during the startup technology
    // transition those temporary vectors are being reshaped by the plater and
    // are not safe to snapshot here. Copy the stable object graph explicitly.
    m_model.clear_objects();
    m_model.objects.reserve(model.objects.size());
    for (const ModelObject *object : model.objects)
        m_model.add_object(*object);
    m_full_print_config = std::move(config);
    // Unlike the FFF/SLA implementations, the independent DLP engine does not
    // consume G-code placeholders. Importing the entire mixed GUI configuration
    // into PlaceholderParser compares legacy vector options left by the previous
    // FFF preset and may dereference an incompatible ConfigOption vector during
    // the deferred technology switch. DLP output naming currently uses the
    // explicit archive path, so keep the parser isolated from that legacy state.
    update_engine_config();
    m_raster_layers.clear();
    m_finished = false;
    ++m_raster_revision;
    return had_results ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED;
}

void DLPPrint::update_engine_config()
{
    if (const auto *opt = config_option<ConfigOptionInt>(m_full_print_config, "display_pixels_x"))
        m_printer_config.resolution.width_px = std::max(0, opt->value);
    if (const auto *opt = config_option<ConfigOptionInt>(m_full_print_config, "display_pixels_y"))
        m_printer_config.resolution.height_px = std::max(0, opt->value);
    if (const auto *opt = config_option<ConfigOptionFloat>(m_full_print_config, "display_width"))
        m_printer_config.build_width_mm = opt->value;
    if (const auto *opt = config_option<ConfigOptionFloat>(m_full_print_config, "display_height"))
        m_printer_config.build_height_mm = opt->value;
    if (const auto *opt = config_option<ConfigOptionBool>(m_full_print_config, "display_mirror_x"))
        m_printer_config.mirror_x = opt->value;
    if (const auto *opt = config_option<ConfigOptionBool>(m_full_print_config, "display_mirror_y"))
        m_printer_config.mirror_y = opt->value;
    if (const auto *opt = config_option<ConfigOptionFloat>(m_full_print_config, "layer_height"))
        m_process_config.layer_height_mm = opt->value;
    if (const auto *opt = config_option<ConfigOptionFloat>(m_full_print_config, "exposure_time"))
        m_process_config.exposure_time_s = opt->value;
    if (const auto *opt = config_option<ConfigOptionFloat>(m_full_print_config, "initial_exposure_time"))
        m_process_config.initial_exposure_time_s = opt->value;
}

void DLPPrint::process()
{
    const std::string error = this->validate();
    if (!error.empty())
        throw std::invalid_argument(error);

    m_finished = false;
    m_raster_layers.clear();
    set_status(0, "Slicing DLP geometry");

    dlp::FixedLayerGeometrySlicer slicer;
    dlp::GrayscaleRasterizer rasterizer;
    const dlp::CancelFn cancel = [this] { this->throw_if_canceled(); };
    std::vector<dlp::Layer> layers = slicer.slice(m_model, m_printer_config, m_process_config, cancel);

    m_raster_layers.reserve(layers.size());
    for (size_t i = 0; i < layers.size(); ++i) {
        cancel();
        layers[i].exposure_time_s = layers[i].index < m_process_config.initial_layer_count ?
            m_process_config.initial_exposure_time_s : m_process_config.exposure_time_s;
        m_raster_layers.emplace_back(rasterizer.rasterize(layers[i], m_printer_config, cancel));
        const int percent = 10 + static_cast<int>(85.0 * double(i + 1) / std::max<size_t>(1, layers.size()));
        set_status(percent, "Rasterizing DLP masks");
    }
    m_finished = true;
    ++m_raster_revision;
    set_status(95, "DLP masks ready");
}

void DLPPrint::export_print(const std::string &output_path) const
{
    if (!m_finished)
        throw std::logic_error("DLP print is not processed");
    dlp::DirectoryArchiveWriter writer;
    writer.begin(m_printer_config, m_process_config, m_raster_layers.size());
    for (const dlp::RasterLayer &layer : m_raster_layers)
        writer.add_layer(dlp::RasterLayer(layer));
    writer.finish(output_path);
}

std::string DLPPrint::output_filename(const std::string &filename_base) const
{
    return this->PrintBase::output_filename("", ".dlp", filename_base);
}

} // namespace Slic3r
