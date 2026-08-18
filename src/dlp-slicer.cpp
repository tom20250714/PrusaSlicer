#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "libslic3r/DLP/DLPBinarySTL.hpp"
#include "libslic3r/DLP/DLPPrint.hpp"
#include <boost/filesystem/path.hpp>

namespace {

std::pair<uint32_t, uint32_t> parse_pair(const std::string &value)
{
    const size_t separator = value.find('x');
    if (separator == std::string::npos)
        throw std::invalid_argument("Expected AxB value: " + value);
    return { static_cast<uint32_t>(std::stoul(value.substr(0, separator))),
             static_cast<uint32_t>(std::stoul(value.substr(separator + 1))) };
}

std::pair<double, double> parse_size(const std::string &value)
{
    const size_t separator = value.find('x');
    if (separator == std::string::npos)
        throw std::invalid_argument("Expected AxB value: " + value);
    return { std::stod(value.substr(0, separator)), std::stod(value.substr(separator + 1)) };
}

} // namespace

int main(int argc, char **argv)
{
    try {
        if (argc < 3) {
            std::cerr << "Usage: dlp-slicer <binary.stl> <output-dir> [--resolution WxH] "
                         "[--build-area WxH] [--layer-height mm] [--mirror-x] [--mirror-y] [--open]\n";
            return 2;
        }
        Slic3r::dlp::PrinterConfig printer { { 1920, 1080 }, 120.0, 67.5 };
        Slic3r::dlp::ProcessConfig process;
        bool open_viewer = false;
        for (int index = 3; index < argc; ++index) {
            const std::string option = argv[index];
            if (option == "--resolution" && index + 1 < argc) {
                const auto value = parse_pair(argv[++index]);
                printer.resolution = { value.first, value.second };
            } else if (option == "--build-area" && index + 1 < argc) {
                const auto value = parse_size(argv[++index]);
                printer.build_width_mm = value.first;
                printer.build_height_mm = value.second;
            } else if (option == "--layer-height" && index + 1 < argc) {
                process.layer_height_mm = std::stod(argv[++index]);
            } else if (option == "--mirror-x") {
                printer.mirror_x = true;
            } else if (option == "--mirror-y") {
                printer.mirror_y = true;
            } else if (option == "--open") {
                open_viewer = true;
            } else {
                throw std::invalid_argument("Unknown or incomplete option: " + option);
            }
        }
        if (const std::string error = Slic3r::dlp::Print::validate(printer, process); ! error.empty())
            throw std::invalid_argument(error);

        const Slic3r::dlp::ExportResult result = Slic3r::dlp::export_layers(
            Slic3r::dlp::slice_binary_stl(argv[1], process), printer, process, argv[2]);
        std::cout << "Wrote " << result.layer_count << " DLP layers to " << argv[2] << '\n';
#ifdef _WIN32
        if (open_viewer) {
            const std::string viewer = (boost::filesystem::path(argv[2]) / "viewer.html").string();
            if (reinterpret_cast<intptr_t>(ShellExecuteA(nullptr, "open", viewer.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                throw std::runtime_error("Could not open DLP layer viewer");
        }
#else
        if (open_viewer)
            std::cout << "Open " << argv[2] << "/viewer.html in a browser.\n";
#endif
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "dlp-slicer: " << error.what() << '\n';
        return 1;
    }
}
