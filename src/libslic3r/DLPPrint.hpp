///|/ PrusaSlicer PrintBase adapter for the independent DLP engine.
#ifndef slic3r_DLPPrintBase_hpp_
#define slic3r_DLPPrintBase_hpp_

#include <vector>
#include <cstdint>

#include "PrintBase.hpp"
#include "DLP/DLPPrint.hpp"

namespace Slic3r {

class DLPPrint final : public PrintBase
{
public:
    PrinterTechnology technology() const noexcept override { return ptDLP; }

    void clear() override;
    bool empty() const override;
    std::vector<ObjectID> print_object_ids() const override;
    std::string validate(std::vector<std::string> *warnings = nullptr) const override;
    ApplyStatus apply(const Model &, DynamicPrintConfig,
                      std::vector<std::string> *warnings = nullptr,
                      const DynamicPrintConfig *original_config = nullptr) override;
    void set_task(const TaskParams &) override {}
    void process() override;
    void finalize() override {}
    void cleanup() override {}
    bool finished() const override { return m_finished; }
    std::string output_filename(const std::string &filename_base = {}) const override;

    // Writes the engine-neutral validation archive (PNG masks + manifest + viewer).
    void export_print(const std::string &output_path) const;
    const std::vector<dlp::RasterLayer>& raster_layers() const noexcept { return m_raster_layers; }
    uint64_t raster_revision() const noexcept { return m_raster_revision; }

private:
    void update_engine_config();

    dlp::PrinterConfig          m_printer_config;
    dlp::ProcessConfig          m_process_config;
    std::vector<dlp::RasterLayer> m_raster_layers;
    bool                        m_finished { false };
    uint64_t                    m_raster_revision { 0 };
};

} // namespace Slic3r

#endif
