// TopoWeave core - High Poly reference surface.
//
// A Surface owns a triangle soup (normally the merged High Poly sources,
// already transformed into the retopology object's space), a BVH, and smooth
// vertex normals. Every projection-aware tool goes through this class.
#pragma once

#include "rtp/Math.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rtp {

struct TriMesh {
    std::vector<Vec3> verts;
    std::vector<std::array<int, 3>> tris;

    // Appends another mesh (used to merge multiple High Poly sources).
    void append(const TriMesh& other);
    Aabb bounds() const;
    bool empty() const { return tris.empty(); }
};

struct SurfaceHit {
    bool valid = false;
    int tri = -1;
    double t = kInf;      // ray parameter (raycast) or distance (closest point)
    double u = 0, v = 0;  // barycentrics: p = (1-u-v)*A + u*B + v*C
    Vec3 point;
    Vec3 normal;          // smooth (interpolated) unit normal
};

// One closed or open polyline produced by slicing the surface with a plane.
struct SliceLoop {
    std::vector<Vec3> points;
    bool closed = false;
};

class Surface {
public:
    Surface() = default;
    explicit Surface(TriMesh mesh) { build(std::move(mesh)); }

    void build(TriMesh mesh);
    void clear();
    bool empty() const { return mesh_.tris.empty(); }

    const TriMesh& mesh() const { return mesh_; }
    const Aabb& bounds() const { return bounds_; }
    // Characteristic size used to derive relative tolerances.
    double scale() const { return bounds_.diagonal(); }

    // Nearest hit along the ray in [0, tMax]. Back faces count as hits.
    bool raycast(const Ray& ray, SurfaceHit& hit, double tMax = kInf) const;

    // Closest surface point to p within maxDist.
    bool closestPoint(const Vec3& p, SurfaceHit& hit, double maxDist = kInf) const;

    // Projects p onto the surface and lifts it by offset along the smooth normal.
    // If a direction hint is supplied the point is first shot along +/-hint
    // (more stable on thin or concave regions); otherwise the closest point is used.
    bool project(const Vec3& p, double offset, Vec3& out, const Vec3* dirHint = nullptr,
                 double searchDist = kInf) const;

    Vec3 smoothNormal(int tri, double u, double v) const;
    Vec3 faceNormal(int tri) const;

    // Cuts the surface with a plane and chains the cut segments into polylines.
    std::vector<SliceLoop> slice(const Plane& plane) const;

private:
    struct Node {
        Aabb box;
        int left = -1, right = -1;  // children (internal nodes)
        int first = 0, count = 0;   // triangle range (leaf nodes, count > 0)
    };

    void buildNode(int nodeIndex, int first, int count, int depth);
    bool rayTri(int tri, const Ray& ray, double tMax, SurfaceHit& hit) const;
    Vec3 closestOnTri(int tri, const Vec3& p, double& u, double& v) const;
    void fillHit(SurfaceHit& hit) const;

    TriMesh mesh_;
    std::vector<Vec3> vertNormals_;
    std::vector<Vec3> faceNormals_;
    std::vector<Aabb> triBoxes_;
    std::vector<Vec3> triCentroids_;
    std::vector<int> triOrder_;
    std::vector<Node> nodes_;
    Aabb bounds_;
};

}  // namespace rtp
