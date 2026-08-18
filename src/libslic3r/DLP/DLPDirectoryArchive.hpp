///|/ Human-readable validation archive for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DLPDirectoryArchive_hpp_
#define slic3r_DLPDirectoryArchive_hpp_

#include <string>
#include <utility>
#include <vector>

#include "DLPPrint.hpp"

namespace Slic3r::dlp {

// Writes a directory containing manifest.json and one grayscale PNG per layer.
// This format is intended for engine validation, not for a specific printer.
class DirectoryArchiveWriter final : public ArchiveWriter
{
public:
    explicit DirectoryArchiveWriter(OutputConfig output = {}) : m_output(std::move(output)) {}
    void begin(const PrinterConfig &, const ProcessConfig &, size_t layer_count) override;
    void begin_output(const PrinterConfig &, const ProcessConfig &, size_t layer_count,
                      const std::string &output_path) override;
    // Selects the destination before rasterization so add_layer() can write
    // each PNG immediately instead of retaining every pixel buffer in memory.
    void begin_streaming(const PrinterConfig &, const ProcessConfig &, size_t layer_count,
                         const std::string &output_path);
    void add_layer(RasterLayer &&) override;
    void finish(const std::string &output_path) override;
    size_t layer_count() const { return m_written_layers.size() + m_layers.size(); }

private:
    struct LayerMetadata {
        uint32_t    index { 0 };
        double      height_mm { 0.0 };
        double      exposure_time_s { 0.0 };
        std::string filename;
    };

    PrinterConfig           m_printer;
    ProcessConfig           m_process;
    OutputConfig            m_output;
    size_t                  m_expected_layers { 0 };
    std::vector<RasterLayer> m_layers;
    std::vector<LayerMetadata> m_written_layers;
    std::string             m_streaming_output_path;
    bool                    m_streaming { false };
    bool                    m_started { false };
};

} // namespace Slic3r::dlp

#endif
