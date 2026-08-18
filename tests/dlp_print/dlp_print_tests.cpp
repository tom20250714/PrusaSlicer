#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <utility>
#include <iterator>

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "libslic3r/DLP/DLPPrint.hpp"
#ifndef SLIC3R_DLP_CORE_ONLY
#include "libslic3r/DLP/DLPGeometrySlicer.hpp"
#endif
#include "libslic3r/DLP/DLPRasterizer.hpp"
#include "libslic3r/DLP/DLPDirectoryArchive.hpp"
#include "libslic3r/PNGReadWrite.hpp"
#ifndef SLIC3R_DLP_CORE_ONLY
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#endif

#ifdef SLIC3R_DLP_CORE_ONLY
namespace Slic3r { class Model {}; }
#endif

using namespace Slic3r;

namespace {

class Slicer final : public dlp::GeometrySlicer
{
public:
    std::vector<dlp::Layer> slice(const Model &, const dlp::PrinterConfig &,
                                  const dlp::ProcessConfig &, const dlp::CancelFn &) const override
    {
        return { { 0, 0.05 }, { 0, 0.10 } };
    }
};

class Supports final : public dlp::SupportGenerator
{
public:
    void generate(const Model &, std::vector<dlp::Layer> &, const dlp::PrinterConfig &,
                  const dlp::ProcessConfig &, const dlp::CancelFn &) const override {}
};

class Rasterizer final : public dlp::Rasterizer
{
public:
    dlp::RasterLayer rasterize(const dlp::Layer &, const dlp::PrinterConfig &printer,
                               const dlp::CancelFn &) const override
    {
        dlp::RasterLayer out;
        out.grayscale.resize(printer.resolution.width_px * printer.resolution.height_px);
        return out;
    }
};

class Corrector final : public dlp::OpticalCorrector
{
public:
    void correct(dlp::RasterLayer &, const dlp::PrinterConfig &, const dlp::CancelFn &) const override {}
};

class Writer final : public dlp::ArchiveWriter
{
public:
    void begin(const dlp::PrinterConfig &, const dlp::ProcessConfig &, size_t count) override
    {
        expected_count = count;
    }
    void add_layer(dlp::RasterLayer &&layer) override { layers.emplace_back(std::move(layer)); }
    void finish(const std::string &path) override { output_path = path; }

    size_t                        expected_count { 0 };
    std::vector<dlp::RasterLayer> layers;
    std::string                   output_path;
};

} // namespace

TEST_CASE("DLP pipeline coordinates independent components", "[DLPPrint]")
{
    Slicer slicer;
    Supports supports;
    Rasterizer rasterizer;
    Corrector corrector;
    Writer writer;
    dlp::Print print(slicer, supports, rasterizer, corrector, writer);

    dlp::PrinterConfig printer { { 4, 3 }, 40.0, 30.0 };
    dlp::ProcessConfig process;
    process.initial_layer_count   = 1;
    process.exposure_time_s       = 2.5;
    process.initial_exposure_time_s = 25.0;

    Model model;
    print.process(model, printer, process, "output.dlp");

    REQUIRE(writer.expected_count == 2);
    REQUIRE(writer.layers.size() == 2);
    CHECK(writer.layers[0].index == 0);
    CHECK(writer.layers[0].exposure_time_s == 25.0);
    CHECK(writer.layers[1].index == 1);
    CHECK(writer.layers[1].exposure_time_s == 2.5);
    CHECK(writer.layers[1].grayscale.size() == 12);
    CHECK(writer.output_path == "output.dlp");
}

TEST_CASE("DLP configuration rejects invalid projector dimensions", "[DLPPrint]")
{
    dlp::PrinterConfig printer;
    dlp::ProcessConfig process;
    CHECK_FALSE(dlp::Print::validate(printer, process).empty());
}

#ifndef SLIC3R_DLP_CORE_ONLY
TEST_CASE("DLP fixed layer slicer creates cross-sections from model instances", "[DLPGeometrySlicer]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(2.0, 3.0, 1.0));
    object->add_instance();

    dlp::PrinterConfig printer { { 40, 30 }, 20.0, 15.0 };
    dlp::ProcessConfig process;
    process.layer_height_mm = 0.25;

    dlp::FixedLayerGeometrySlicer slicer;
    const std::vector<dlp::Layer> layers = slicer.slice(model, printer, process, {});

    REQUIRE(layers.size() == 4);
    CHECK(layers.front().height_mm == Catch::Approx(0.125));
    CHECK(layers.back().height_mm == Catch::Approx(0.875));
    for (const dlp::Layer &layer : layers)
        CHECK_FALSE(layer.model.empty());
}

TEST_CASE("DLP fixed layer slicer returns no layers for an empty model", "[DLPGeometrySlicer]")
{
    dlp::PrinterConfig printer { { 40, 30 }, 20.0, 15.0 };
    dlp::ProcessConfig process;
    Model model;

    dlp::FixedLayerGeometrySlicer slicer;
    CHECK(slicer.slice(model, printer, process, {}).empty());
}
#endif

TEST_CASE("DLP rasterizer maps build coordinates to pixels", "[DLPRasterizer]")
{
    dlp::Layer layer;
    layer.model.emplace_back(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(2.0, 0.0),
        Point::new_scale(2.0, 4.0), Point::new_scale(0.0, 4.0)
    });
    dlp::PrinterConfig printer { { 4, 4 }, 4.0, 4.0 };

    dlp::GrayscaleRasterizer rasterizer;
    const dlp::RasterLayer raster = rasterizer.rasterize(layer, printer, {});

    REQUIRE(raster.grayscale.size() == 16);
    for (size_t y = 0; y < 4; ++y) {
        CHECK(raster.grayscale[y * 4] == 255);
        CHECK(raster.grayscale[y * 4 + 1] == 255);
        CHECK(raster.grayscale[y * 4 + 2] == 0);
        CHECK(raster.grayscale[y * 4 + 3] == 0);
    }
}

TEST_CASE("DLP rasterizer mirrors the exposure mask", "[DLPRasterizer]")
{
    dlp::Layer layer;
    layer.model.emplace_back(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(1.0, 0.0),
        Point::new_scale(1.0, 1.0), Point::new_scale(0.0, 1.0)
    });
    dlp::PrinterConfig printer { { 4, 1 }, 4.0, 1.0 };
    printer.mirror_x = true;

    dlp::GrayscaleRasterizer rasterizer;
    const dlp::RasterLayer raster = rasterizer.rasterize(layer, printer, {});

    const std::vector<uint8_t> expected { 0, 0, 0, 255 };
    CHECK(raster.grayscale == expected);
}

TEST_CASE("DLP rasterizer preserves polygon holes", "[DLPRasterizer]")
{
    Polygon contour(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(4.0, 0.0),
        Point::new_scale(4.0, 4.0), Point::new_scale(0.0, 4.0)
    });
    Polygon hole(Points {
        Point::new_scale(1.0, 1.0), Point::new_scale(1.0, 3.0),
        Point::new_scale(3.0, 3.0), Point::new_scale(3.0, 1.0)
    });
    dlp::Layer layer;
    layer.model.emplace_back(std::move(contour), std::move(hole));
    dlp::PrinterConfig printer { { 4, 4 }, 4.0, 4.0 };

    dlp::GrayscaleRasterizer rasterizer;
    const dlp::RasterLayer raster = rasterizer.rasterize(layer, printer, {});

    CHECK(raster.grayscale[0] == 255);
    CHECK(raster.grayscale[5] == 0);
    CHECK(raster.grayscale[6] == 0);
    CHECK(raster.grayscale[9] == 0);
    CHECK(raster.grayscale[10] == 0);
    CHECK(raster.grayscale[15] == 255);
}

TEST_CASE("DLP rasterizer supersamples polygon edges", "[DLPRasterizer]")
{
    dlp::Layer layer;
    layer.model.emplace_back(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(0.5, 0.0),
        Point::new_scale(0.5, 1.0), Point::new_scale(0.0, 1.0)
    });
    dlp::PrinterConfig printer { { 1, 1 }, 1.0, 1.0 };
    printer.antialiasing_samples = 2;

    dlp::GrayscaleRasterizer rasterizer;
    const dlp::RasterLayer raster = rasterizer.rasterize(layer, printer, {});

    REQUIRE(raster.grayscale.size() == 1);
    CHECK(raster.grayscale.front() == 128);
}

TEST_CASE("DLP directory archive writes PNG layers and a manifest", "[DLPArchive]")
{
    const boost::filesystem::path output = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("prusaslicer-dlp-%%%%-%%%%");
    struct Cleanup {
        boost::filesystem::path path;
        ~Cleanup() { boost::system::error_code ec; boost::filesystem::remove_all(path, ec); }
    } cleanup { output };

    dlp::PrinterConfig printer { { 2, 1 }, 2.0, 1.0 };
    dlp::ProcessConfig process;
    dlp::DirectoryArchiveWriter writer;
    writer.begin(printer, process, 2);

    for (uint32_t index = 0; index < 2; ++index) {
        dlp::RasterLayer layer;
        layer.index = index;
        layer.height_mm = 0.025 + 0.05 * index;
        layer.exposure_time_s = index == 0 ? 20.0 : 2.0;
        layer.resolution = printer.resolution;
        layer.grayscale = { uint8_t(index * 255), uint8_t(255 - index * 255) };
        writer.add_layer(std::move(layer));
    }
    writer.finish(output.string());

    CHECK(boost::filesystem::is_regular_file(output / "Preview_t.png"));
    CHECK(boost::filesystem::is_regular_file(output / "SEC_0001.png"));
    CHECK(boost::filesystem::is_regular_file(output / "SEC_0002.png"));
    REQUIRE(boost::filesystem::is_regular_file(output / "manifest.json"));
    REQUIRE(boost::filesystem::is_regular_file(output / "viewer.html"));

    boost::nowide::ifstream stream((output / "manifest.json").string());
    const nlohmann::json manifest = nlohmann::json::parse(stream);
    CHECK(manifest["format"] == "PrusaSlicer-DLP-Validation");
    CHECK(manifest["resolution"]["width_px"] == 2);
    REQUIRE(manifest["layers"].size() == 2);
    CHECK(manifest["preview"] == "Preview_t.png");
    CHECK(manifest["layers"][0]["file"] == "SEC_0001.png");
    CHECK(manifest["layers"][1]["exposure_time_s"] == 2.0);

    boost::nowide::ifstream viewer((output / "viewer.html").string());
    const std::string html((std::istreambuf_iterator<char>(viewer)), std::istreambuf_iterator<char>());
    CHECK(html.find("id=\"layer-slider\"") != std::string::npos);
    CHECK(html.find("SEC_0001.png") != std::string::npos);
}

TEST_CASE("DLP directory archive streams PNG layers as they arrive", "[DLPArchive]")
{
    const boost::filesystem::path output = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("prusaslicer-dlp-stream-%%%%-%%%%");
    struct Cleanup {
        boost::filesystem::path path;
        ~Cleanup() { boost::system::error_code ec; boost::filesystem::remove_all(path, ec); }
    } cleanup { output };

    dlp::PrinterConfig printer { { 2, 1 }, 2.0, 1.0 };
    dlp::ProcessConfig process;
    dlp::DirectoryArchiveWriter writer;
    writer.begin_streaming(printer, process, 2, output.string());

    for (uint32_t index = 0; index < 2; ++index) {
        dlp::RasterLayer layer;
        layer.index = index;
        layer.height_mm = 0.025 + 0.05 * index;
        layer.exposure_time_s = 2.0;
        layer.resolution = printer.resolution;
        layer.grayscale = {255, 0};
        writer.add_layer(std::move(layer));
        CHECK(boost::filesystem::is_regular_file(
            output / (index == 0 ? "SEC_0001.png" : "SEC_0002.png")));
    }
    CHECK(boost::filesystem::is_regular_file(output / "Preview_t.png"));
    CHECK_FALSE(boost::filesystem::exists(output / "manifest.json"));

    writer.finish(output.string());
    REQUIRE(boost::filesystem::is_regular_file(output / "manifest.json"));
    boost::nowide::ifstream stream((output / "manifest.json").string());
    const nlohmann::json manifest = nlohmann::json::parse(stream);
    CHECK(manifest["layers"].size() == 2);
}

TEST_CASE("DLP directory archive supports controller-only output naming", "[DLPArchive]")
{
    const boost::filesystem::path output = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("prusaslicer-dlp-controller-%%%%-%%%%");
    struct Cleanup {
        boost::filesystem::path path;
        ~Cleanup() { boost::system::error_code ec; boost::filesystem::remove_all(path, ec); }
    } cleanup { output };

    dlp::OutputConfig config;
    config.layer_prefix = "LAYER-";
    config.first_layer_number = 0;
    config.minimum_number_digits = 3;
    config.write_preview = false;
    config.write_manifest = false;
    config.write_viewer = false;
    dlp::DirectoryArchiveWriter writer(config);
    dlp::PrinterConfig printer {{2, 1}, 2.0, 1.0};
    dlp::ProcessConfig process;
    writer.begin_streaming(printer, process, 1, output.string());
    dlp::RasterLayer layer;
    layer.resolution = printer.resolution;
    layer.grayscale = {0, 255};
    writer.add_layer(std::move(layer));
    writer.finish(output.string());

    CHECK(boost::filesystem::is_regular_file(output / "LAYER-000.png"));
    CHECK_FALSE(boost::filesystem::exists(output / "Preview_t.png"));
    CHECK_FALSE(boost::filesystem::exists(output / "manifest.json"));
    CHECK_FALSE(boost::filesystem::exists(output / "viewer.html"));
}

TEST_CASE("DLP export golden PNG distinguishes preserved and centered placement", "[DLPGolden]")
{
    const boost::filesystem::path root = boost::filesystem::temp_directory_path() /
        boost::filesystem::unique_path("prusaslicer-dlp-golden-%%%%-%%%%");
    struct Cleanup {
        boost::filesystem::path path;
        ~Cleanup() { boost::system::error_code ec; boost::filesystem::remove_all(path, ec); }
    } cleanup { root };

    dlp::Layer layer;
    layer.height_mm = 0.05;
    layer.model.emplace_back(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(2.0, 0.0),
        Point::new_scale(2.0, 1.0), Point::new_scale(0.0, 1.0)
    });
    dlp::PrinterConfig printer {{4, 1}, 4.0, 1.0};
    dlp::ProcessConfig process;

    const auto export_red_pixels = [&](dlp::PlacementMode placement, const char *name) {
        dlp::ExecutionConfig execution;
        execution.placement = placement;
        execution.max_parallel_layers = 1;
        execution.output.write_preview = false;
        execution.output.write_manifest = false;
        execution.output.write_viewer = false;
        const boost::filesystem::path output = root / name;
        const dlp::ExportResult result = dlp::export_layers({layer}, printer, process,
                                                            output.string(), execution);
        REQUIRE(result.layer_count == 1);
        boost::nowide::ifstream stream((output / "SEC_0001.png").string(), std::ios::binary);
        const std::string encoded((std::istreambuf_iterator<char>(stream)),
                                  std::istreambuf_iterator<char>());
        std::vector<unsigned char> rgba;
        unsigned width = 0, height = 0;
        REQUIRE(png::decode_png(encoded, rgba, width, height));
        REQUIRE(width == 4);
        REQUIRE(height == 1);
        std::vector<uint8_t> red;
        for (size_t index = 0; index < width; ++index)
            red.push_back(rgba[index * 4]);
        return red;
    };

    CHECK(export_red_pixels(dlp::PlacementMode::PreserveModelPosition, "preserved.dlp") ==
          std::vector<uint8_t>{255, 255, 0, 0});
    CHECK(export_red_pixels(dlp::PlacementMode::CenterOnBuildArea, "centered.dlp") ==
          std::vector<uint8_t>{0, 255, 255, 0});
}

TEST_CASE("DLP rasterizer unions model and support exposure", "[DLPGolden]")
{
    dlp::Layer layer;
    layer.model.emplace_back(Points {
        Point::new_scale(0.0, 0.0), Point::new_scale(1.0, 0.0),
        Point::new_scale(1.0, 1.0), Point::new_scale(0.0, 1.0)
    });
    layer.supports.emplace_back(Points {
        Point::new_scale(2.0, 0.0), Point::new_scale(3.0, 0.0),
        Point::new_scale(3.0, 1.0), Point::new_scale(2.0, 1.0)
    });
    dlp::PrinterConfig printer {{4, 1}, 4.0, 1.0};
    const dlp::RasterLayer raster = dlp::GrayscaleRasterizer().rasterize(layer, printer, {});
    CHECK(raster.grayscale == std::vector<uint8_t>{255, 0, 255, 0});
}
