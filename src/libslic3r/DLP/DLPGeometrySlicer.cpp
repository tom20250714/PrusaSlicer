///|/ Fixed-height geometry slicer for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "DLPGeometrySlicer.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace Slic3r::dlp {

std::vector<Layer> FixedLayerGeometrySlicer::slice(const Model &model, const PrinterConfig &,
                                                   const ProcessConfig &process,
                                                   const CancelFn &cancel) const
{
    if (cancel)
        cancel();
    if (! std::isfinite(process.layer_height_mm) || process.layer_height_mm <= 0.0)
        throw std::invalid_argument("DLP layer height must be finite and greater than zero");

    TriangleMesh mesh = model.mesh();
    if (mesh.empty())
        return {};

    const BoundingBoxf3 bounds = mesh.bounding_box();
    if (! bounds.defined || ! std::isfinite(bounds.max.z()))
        throw std::runtime_error("DLP model has invalid bounds");
    if (bounds.max.z() <= 0.0)
        return {};

    const double layer_count_d = std::ceil(bounds.max.z() / process.layer_height_mm);
    if (! std::isfinite(layer_count_d) ||
        layer_count_d > static_cast<double>(std::numeric_limits<uint32_t>::max()))
        throw std::overflow_error("DLP model requires too many layers");

    const size_t layer_count = static_cast<size_t>(layer_count_d);
    std::vector<float> slice_zs;
    slice_zs.reserve(layer_count);
    for (size_t index = 0; index < layer_count; ++index)
        slice_zs.emplace_back(static_cast<float>((static_cast<double>(index) + 0.5) * process.layer_height_mm));

    std::vector<ExPolygons> sections = slice_mesh_ex(mesh.its, slice_zs, cancel ? cancel : CancelFn([] {}));
    if (sections.size() != slice_zs.size())
        throw std::runtime_error("DLP geometry slicer returned an unexpected layer count");

    std::vector<Layer> layers;
    layers.reserve(layer_count);
    for (size_t index = 0; index < layer_count; ++index) {
        if (cancel)
            cancel();
        Layer layer;
        layer.index     = static_cast<uint32_t>(index);
        layer.height_mm = slice_zs[index];
        layer.model     = union_ex(sections[index]);
        layers.emplace_back(std::move(layer));
    }
    return layers;
}

} // namespace Slic3r::dlp
