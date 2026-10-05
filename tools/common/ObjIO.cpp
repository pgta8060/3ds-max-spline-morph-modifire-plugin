#include "ObjIO.h"

#include <cstdio>

namespace rtp {
namespace kit {

bool writeObj(const std::string& path, const PolyMesh& mesh) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# TopoWeave retopology\n");
    for (const auto& v : mesh.verts) std::fprintf(f, "v %.6f %.6f %.6f\n", v.x, v.y, v.z);
    for (const auto& face : mesh.faces) {
        std::fprintf(f, "f");
        for (int v : face) std::fprintf(f, " %d", v + 1);
        std::fprintf(f, "\n");
    }
    std::fclose(f);
    return true;
}

bool writeObj(const std::string& path, const TriMesh& mesh) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    for (const auto& v : mesh.verts) std::fprintf(f, "v %.6f %.6f %.6f\n", v.x, v.y, v.z);
    for (const auto& t : mesh.tris) std::fprintf(f, "f %d %d %d\n", t[0] + 1, t[1] + 1, t[2] + 1);
    std::fclose(f);
    return true;
}

bool writeObj(const std::string& path, const std::vector<Polyline>& lines) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    int base = 1;
    for (const auto& l : lines) {
        for (const auto& p : l.pts) std::fprintf(f, "v %.6f %.6f %.6f\n", p.x, p.y, p.z);
        std::fprintf(f, "l");
        for (int i = 0; i < l.size(); ++i) std::fprintf(f, " %d", base + i);
        if (l.closed) std::fprintf(f, " %d", base);
        std::fprintf(f, "\n");
        base += l.size();
    }
    std::fclose(f);
    return true;
}

}  // namespace kit
}  // namespace rtp
