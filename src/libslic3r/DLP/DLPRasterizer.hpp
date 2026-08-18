///|/ Rasterizer for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DLPRasterizer_hpp_
#define slic3r_DLPRasterizer_hpp_

#include "DLPPrint.hpp"

namespace Slic3r::dlp {

// Converts DLP polygons to a row-major 8-bit exposure mask. Row zero maps to
// the build area's minimum Y coordinate before optional mirroring.
class GrayscaleRasterizer final : public Rasterizer
{
public:
    RasterLayer rasterize(const Layer &, const PrinterConfig &, const CancelFn &) const override;
};

} // namespace Slic3r::dlp

#endif
