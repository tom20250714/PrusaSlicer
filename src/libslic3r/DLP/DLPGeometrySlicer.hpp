///|/ Fixed-height geometry slicer for the independent DLP engine.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_DLPGeometrySlicer_hpp_
#define slic3r_DLPGeometrySlicer_hpp_

#include "DLPPrint.hpp"

namespace Slic3r::dlp {

// Produces one cross-section at the center of every exposure layer. The
// implementation shares PrusaSlicer's generic mesh and polygon primitives,
// but does not call the SLA print pipeline.
class FixedLayerGeometrySlicer final : public GeometrySlicer
{
public:
    std::vector<Layer> slice(const Model &, const PrinterConfig &, const ProcessConfig &,
                             const CancelFn &) const override;
};

} // namespace Slic3r::dlp

#endif
