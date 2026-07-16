///|/ Human-readable validation archive for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DLPDirectoryArchive.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/PNGReadWrite.hpp"

namespace Slic3r::dlp {

namespace {

std::string layer_filename(size_t index, const OutputConfig &output)
{
    std::ostringstream name;
    name << output.layer_prefix
         << std::setw(output.minimum_number_digits) << std::setfill('0')
         << (index + output.first_layer_number)
         << output.layer_extension;
    return name.str();
}

void validate_output_config(const OutputConfig &output)
{
    const auto contains_separator = [](const std::string &value) {
        return value.find('/') != std::string::npos || value.find('\\') != std::string::npos;
    };
    if (output.layer_prefix.empty() || output.layer_extension.empty() ||
        contains_separator(output.layer_prefix) || contains_separator(output.layer_extension))
        throw std::invalid_argument("DLP layer filename components must be non-empty base names");
    if (output.minimum_number_digits == 0 || output.minimum_number_digits > 12)
        throw std::invalid_argument("DLP layer number digits must be between 1 and 12");
    if (output.write_preview &&
        (output.preview_filename.empty() || contains_separator(output.preview_filename)))
        throw std::invalid_argument("DLP preview filename must be a non-empty base name");
}

} // namespace

void DirectoryArchiveWriter::begin(const PrinterConfig &printer, const ProcessConfig &process,
                                   size_t layer_count)
{
    m_printer        = printer;
    m_process        = process;
    m_expected_layers = layer_count;
    m_layers.clear();
    m_layers.reserve(layer_count);
    m_written_layers.clear();
    m_written_layers.reserve(layer_count);
    m_streaming_output_path.clear();
    m_streaming = false;
    m_started = true;
}

void DirectoryArchiveWriter::begin_streaming(const PrinterConfig &printer, const ProcessConfig &process,
                                             size_t layer_count, const std::string &output_path)
{
    validate_output_config(m_output);
    if (output_path.empty())
        throw std::invalid_argument("DLP archive output path must not be empty");
    const boost::filesystem::path directory(output_path);
    if (boost::filesystem::exists(directory)) {
        if (! boost::filesystem::is_directory(directory) || ! boost::filesystem::is_empty(directory))
            throw std::runtime_error("DLP archive output directory already exists and is not empty");
    } else if (! boost::filesystem::create_directories(directory)) {
        throw std::runtime_error("Could not create DLP archive output directory");
    }

    begin(printer, process, layer_count);
    m_streaming_output_path = output_path;
    m_streaming = true;
}

void DirectoryArchiveWriter::begin_output(const PrinterConfig &printer, const ProcessConfig &process,
                                          size_t layer_count, const std::string &output_path)
{
    begin_streaming(printer, process, layer_count, output_path);
}

void DirectoryArchiveWriter::add_layer(RasterLayer &&layer)
{
    if (! m_started)
        throw std::logic_error("DLP archive has not been started");
    const size_t received_layers = m_streaming ? m_written_layers.size() : m_layers.size();
    if (received_layers >= m_expected_layers)
        throw std::logic_error("DLP archive received more layers than declared");
    if (layer.index != received_layers)
        throw std::invalid_argument("DLP archive layers must be added in index order");
    if (layer.resolution.width_px != m_printer.resolution.width_px ||
        layer.resolution.height_px != m_printer.resolution.height_px)
        throw std::invalid_argument("DLP archive layer resolution does not match the printer");
    const size_t expected_pixels = size_t(m_printer.resolution.width_px) * m_printer.resolution.height_px;
    if (layer.grayscale.size() != expected_pixels)
        throw std::invalid_argument("DLP archive layer has an invalid pixel buffer size");
    if (! m_streaming) {
        m_layers.emplace_back(std::move(layer));
        return;
    }

    const boost::filesystem::path directory(m_streaming_output_path);
    if (received_layers == 0 && m_output.write_preview) {
        const boost::filesystem::path preview_path = directory / m_output.preview_filename;
        if (! png::write_gray_to_file_fast(preview_path.string(), layer.resolution.width_px,
                                           layer.resolution.height_px, layer.grayscale))
            throw std::runtime_error("Could not write DLP preview PNG: " + preview_path.string());
    }
    const std::string filename = layer_filename(received_layers, m_output);
    const boost::filesystem::path image_path = directory / filename;
    if (! png::write_gray_to_file_fast(image_path.string(), layer.resolution.width_px,
                                       layer.resolution.height_px, layer.grayscale))
        throw std::runtime_error("Could not write DLP layer PNG: " + image_path.string());
    m_written_layers.push_back({layer.index, layer.height_mm, layer.exposure_time_s, filename});
}

void DirectoryArchiveWriter::finish(const std::string &output_path)
{
    if (! m_started)
        throw std::logic_error("DLP archive has not been started");
    const size_t received_layers = m_streaming ? m_written_layers.size() : m_layers.size();
    if (received_layers != m_expected_layers)
        throw std::logic_error("DLP archive did not receive the declared number of layers");
    if (output_path.empty())
        throw std::invalid_argument("DLP archive output path must not be empty");

    const boost::filesystem::path directory(output_path);
    if (m_streaming) {
        if (boost::filesystem::path(m_streaming_output_path) != directory)
            throw std::invalid_argument("DLP streaming output path changed before finish");
    } else {
        if (boost::filesystem::exists(directory)) {
            if (! boost::filesystem::is_directory(directory) || ! boost::filesystem::is_empty(directory))
                throw std::runtime_error("DLP archive output directory already exists and is not empty");
        } else if (! boost::filesystem::create_directories(directory)) {
            throw std::runtime_error("Could not create DLP archive output directory");
        }
    }

    nlohmann::json manifest;
    manifest["format"] = "PrusaSlicer-DLP-Validation";
    manifest["version"] = 1;
    manifest["resolution"] = {
        { "width_px", m_printer.resolution.width_px },
        { "height_px", m_printer.resolution.height_px }
    };
    manifest["build_area_mm"] = {
        { "width", m_printer.build_width_mm },
        { "height", m_printer.build_height_mm }
    };
    manifest["mirror_x"] = m_printer.mirror_x;
    manifest["mirror_y"] = m_printer.mirror_y;
    manifest["antialiasing_samples"] = m_printer.antialiasing_samples;
    manifest["pixel_layout"] = {
        { "storage", "row-major" },
        { "row_zero", "build-area-min-y" }
    };
    manifest["layer_height_mm"] = m_process.layer_height_mm;
    if (m_output.write_preview)
        manifest["preview"] = m_output.preview_filename;
    manifest["layers"] = nlohmann::json::array();

    // The controller expects a standalone preview image next to the sections.
    // Until a camera-rendered thumbnail is wired in, use the first projected
    // section so the preview has the exact target resolution and orientation.
    if (!m_streaming && m_output.write_preview && !m_layers.empty()) {
        const RasterLayer &preview = m_layers.front();
        const boost::filesystem::path preview_path = directory / m_output.preview_filename;
        if (!png::write_gray_to_file_fast(preview_path.string(), preview.resolution.width_px,
                                          preview.resolution.height_px, preview.grayscale))
            throw std::runtime_error("Could not write DLP preview PNG: " + preview_path.string());
    }

    for (size_t index = 0; !m_streaming && index < m_layers.size(); ++index) {
        const RasterLayer &layer = m_layers[index];
        const std::string filename = layer_filename(index, m_output);
        const boost::filesystem::path image_path = directory / filename;
        if (! png::write_gray_to_file_fast(image_path.string(), layer.resolution.width_px,
                                           layer.resolution.height_px, layer.grayscale))
            throw std::runtime_error("Could not write DLP layer PNG: " + image_path.string());
        manifest["layers"].push_back({
            { "index", layer.index },
            { "height_mm", layer.height_mm },
            { "exposure_time_s", layer.exposure_time_s },
            { "file", filename }
        });
    }
    if (m_streaming) {
        for (const LayerMetadata &layer : m_written_layers) {
            manifest["layers"].push_back({
                { "index", layer.index },
                { "height_mm", layer.height_mm },
                { "exposure_time_s", layer.exposure_time_s },
                { "file", layer.filename }
            });
        }
    }

    if (m_output.write_manifest) {
        const boost::filesystem::path manifest_path = directory / "manifest.json";
        boost::nowide::ofstream stream(manifest_path.string(), std::ios::binary | std::ios::trunc);
        if (! stream)
            throw std::runtime_error("Could not create DLP archive manifest");
        stream << manifest.dump(2) << '\n';
        stream.close();
        if (! stream)
            throw std::runtime_error("Could not write DLP archive manifest");
    }

    if (m_output.write_viewer) {
        const boost::filesystem::path viewer_path = directory / "viewer.html";
        boost::nowide::ofstream viewer(viewer_path.string(), std::ios::binary | std::ios::trunc);
        if (! viewer)
            throw std::runtime_error("Could not create DLP layer viewer");
        viewer << R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DLP Layer Viewer</title><style>
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;background:#101216;color:#e8eaed;font:14px system-ui,sans-serif}
header{padding:14px 20px;background:#191c22;border-bottom:1px solid #30343b}h1{font-size:18px;margin:0}
main{display:grid;grid-template-columns:minmax(0,1fr) 300px;height:calc(100vh - 53px)}
.canvas{display:flex;align-items:center;justify-content:center;overflow:auto;padding:24px;background:#08090b}
#layer-image{image-rendering:pixelated;max-width:100%;max-height:100%;background:#000;border:1px solid #3b4049;box-shadow:0 8px 30px #000}
.panel{padding:20px;background:#17191e;border-left:1px solid #30343b}.controls{display:flex;gap:8px;align-items:center;margin:14px 0}
button{background:#f57c00;color:#fff;border:0;border-radius:5px;padding:8px 13px;cursor:pointer}input[type=range]{width:100%}
.value{font-variant-numeric:tabular-nums}.grid{display:grid;grid-template-columns:1fr auto;gap:9px;margin-top:20px}.muted{color:#9aa0a6}
@media(max-width:750px){main{grid-template-columns:1fr;grid-template-rows:minmax(0,1fr) auto}.panel{border-left:0;border-top:1px solid #30343b}}
</style></head><body><header><h1>DLP Layer Viewer</h1></header><main><section class="canvas"><img id="layer-image" alt="DLP layer"></section>
<aside class="panel"><label for="layer-slider">Layer <span id="layer-label" class="value"></span></label>
<input id="layer-slider" type="range" min="0" value="0" step="1"><div class="controls"><button id="play">Play</button><button id="previous">&#9664;</button><button id="next">&#9654;</button></div>
<div class="grid"><span class="muted">Z position</span><span id="height" class="value"></span><span class="muted">Exposure</span><span id="exposure" class="value"></span>
<span class="muted">Resolution</span><span id="resolution" class="value"></span><span class="muted">Build area</span><span id="area" class="value"></span></div></aside></main>
<script>const manifest=)HTML" << manifest.dump() << R"HTML(;
const image=document.getElementById('layer-image'),slider=document.getElementById('layer-slider'),label=document.getElementById('layer-label');
const height=document.getElementById('height'),exposure=document.getElementById('exposure');let timer=null;
slider.max=Math.max(0,manifest.layers.length-1);document.getElementById('resolution').textContent=`${manifest.resolution.width_px} × ${manifest.resolution.height_px}`;
document.getElementById('area').textContent=`${manifest.build_area_mm.width} × ${manifest.build_area_mm.height} mm`;
function show(index){if(!manifest.layers.length)return;index=Math.max(0,Math.min(index,manifest.layers.length-1));slider.value=index;const layer=manifest.layers[index];
image.src=layer.file;label.textContent=`${index+1} / ${manifest.layers.length}`;height.textContent=`${layer.height_mm.toFixed(3)} mm`;exposure.textContent=`${layer.exposure_time_s.toFixed(2)} s`;}
slider.oninput=()=>show(+slider.value);document.getElementById('previous').onclick=()=>show(+slider.value-1);document.getElementById('next').onclick=()=>show(+slider.value+1);
document.getElementById('play').onclick=event=>{if(timer){clearInterval(timer);timer=null;event.target.textContent='Play';return;}event.target.textContent='Pause';timer=setInterval(()=>show((+slider.value+1)%manifest.layers.length),180);};
document.addEventListener('keydown',event=>{if(event.key==='ArrowLeft')show(+slider.value-1);if(event.key==='ArrowRight')show(+slider.value+1);});show(0);
</script></body></html>)HTML";
        viewer.close();
        if (! viewer)
            throw std::runtime_error("Could not write DLP layer viewer");
    }

    m_started = false;
    m_streaming = false;
}

} // namespace Slic3r::dlp
