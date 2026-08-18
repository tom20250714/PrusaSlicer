///|/ Minimal binary STL slicer for the standalone DLP CLI.
#ifndef slic3r_DLPBinarySTL_hpp_
#define slic3r_DLPBinarySTL_hpp_

#include <string>
#include <vector>

#include "DLPPrint.hpp"

namespace Slic3r::dlp {

std::vector<Layer> slice_binary_stl(const std::string &path, const ProcessConfig &process,
                                    const CancelFn &cancel = {});

} // namespace Slic3r::dlp

#endif
