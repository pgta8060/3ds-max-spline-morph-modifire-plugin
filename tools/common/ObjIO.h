// Wavefront OBJ export for inspecting results in any DCC (3ds Max included).
#pragma once

#include "rtp/PolyMesh.h"
#include "rtp/Polyline.h"
#include "rtp/Surface.h"

#include <string>
#include <vector>

namespace rtp {
namespace kit {

bool writeObj(const std::string& path, const PolyMesh& mesh);
bool writeObj(const std::string& path, const TriMesh& mesh);
// Polylines as OBJ "l" elements (handy to inspect guides / rails).
bool writeObj(const std::string& path, const std::vector<Polyline>& lines);

}  // namespace kit
}  // namespace rtp
