#include "rtp/Surface.h"

#include <algorithm>
#include <unordered_map>

namespace rtp {

namespace {
constexpr int kLeafSize = 4;
constexpr int kMaxDepth = 48;

inline uint64_t edgeKey(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
}  // namespace

// ---------------------------------------------------------------------------
// TriMesh
// ---------------------------------------------------------------------------
void TriMesh::append(const TriMesh& other) {
    const int base = static_cast<int>(verts.size());
    verts.insert(verts.end(), other.verts.begin(), other.verts.end());
    tris.reserve(tris.size() + other.tris.size());
    for (const auto& t : other.tris) tris.push_back({t[0] + base, t[1] + base, t[2] + base});
}

Aabb TriMesh::bounds() const {
    Aabb b;
    for (const auto& v : verts) b.expand(v);
    return b;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
void Surface::clear() {
    mesh_ = TriMesh();
    vertNormals_.clear();
    faceNormals_.clear();
    triBoxes_.clear();
    triCentroids_.clear();
    triOrder_.clear();
    nodes_.clear();
    bounds_ = Aabb();
}

void Surface::build(TriMesh mesh) {
    clear();
    mesh_ = std::move(mesh);

    // Drop degenerate / out-of-range triangles up front so queries never see them.
    const int nv = static_cast<int>(mesh_.verts.size());
    auto& tris = mesh_.tris;
    tris.erase(std::remove_if(tris.begin(), tris.end(),
                              [&](const std::array<int, 3>& t) {
                                  for (int k = 0; k < 3; ++k)
                                      if (t[k] < 0 || t[k] >= nv) return true;
                                  if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) return true;
                                  const Vec3 n = cross(mesh_.verts[t[1]] - mesh_.verts[t[0]],
                                                       mesh_.verts[t[2]] - mesh_.verts[t[0]]);
                                  return length2(n) <= 1e-30;
                              }),
               tris.end());

    const int nt = static_cast<int>(tris.size());
    faceNormals_.resize(nt);
    triBoxes_.resize(nt);
    triCentroids_.resize(nt);
    triOrder_.resize(nt);
    vertNormals_.assign(nv, Vec3());

    for (int i = 0; i < nt; ++i) {
        const Vec3& a = mesh_.verts[tris[i][0]];
        const Vec3& b = mesh_.verts[tris[i][1]];
        const Vec3& c = mesh_.verts[tris[i][2]];
        const Vec3 n = cross(b - a, c - a);  // length = 2*area -> area weighting
        faceNormals_[i] = normalize(n);
        for (int k = 0; k < 3; ++k) vertNormals_[tris[i][k]] += n;
        Aabb box;
        box.expand(a);
        box.expand(b);
        box.expand(c);
        triBoxes_[i] = box;
        triCentroids_[i] = (a + b + c) / 3.0;
        triOrder_[i] = i;
        bounds_.expand(box);
    }
    for (auto& n : vertNormals_) n = normalize(n);

    if (nt == 0) return;
    nodes_.reserve(static_cast<size_t>(nt) * 2 / kLeafSize + 2);
    nodes_.push_back(Node());
    buildNode(0, 0, nt, 0);
}

void Surface::buildNode(int nodeIndex, int first, int count, int depth) {
    Aabb box, centroidBox;
    for (int i = first; i < first + count; ++i) {
        box.expand(triBoxes_[triOrder_[i]]);
        centroidBox.expand(triCentroids_[triOrder_[i]]);
    }
    nodes_[nodeIndex].box = box;

    if (count <= kLeafSize || depth >= kMaxDepth || centroidBox.diagonal() <= 0.0) {
        nodes_[nodeIndex].first = first;
        nodes_[nodeIndex].count = count;
        return;
    }

    // Median split along the longest centroid axis: robust and O(n log n).
    const int axis = centroidBox.longestAxis();
    const int mid = first + count / 2;
    std::nth_element(triOrder_.begin() + first, triOrder_.begin() + mid,
                     triOrder_.begin() + first + count, [&](int a, int b) {
                         return triCentroids_[a][axis] < triCentroids_[b][axis];
                     });

    const int left = static_cast<int>(nodes_.size());
    nodes_.push_back(Node());
    nodes_.push_back(Node());
    nodes_[nodeIndex].left = left;
    nodes_[nodeIndex].right = left + 1;
    buildNode(left, first, mid - first, depth + 1);
    buildNode(left + 1, mid, first + count - mid, depth + 1);
}

// ---------------------------------------------------------------------------
// Normals
// ---------------------------------------------------------------------------
Vec3 Surface::faceNormal(int tri) const { return faceNormals_[tri]; }

Vec3 Surface::smoothNormal(int tri, double u, double v) const {
    const auto& t = mesh_.tris[tri];
    const Vec3 n = vertNormals_[t[0]] * (1.0 - u - v) + vertNormals_[t[1]] * u + vertNormals_[t[2]] * v;
    return normalize(n, faceNormals_[tri]);
}

void Surface::fillHit(SurfaceHit& hit) const {
    const auto& t = mesh_.tris[hit.tri];
    hit.point = mesh_.verts[t[0]] * (1.0 - hit.u - hit.v) + mesh_.verts[t[1]] * hit.u +
                mesh_.verts[t[2]] * hit.v;
    hit.normal = smoothNormal(hit.tri, hit.u, hit.v);
    hit.valid = true;
}

// ---------------------------------------------------------------------------
// Ray casting (Moller-Trumbore, double sided)
// ---------------------------------------------------------------------------
bool Surface::rayTri(int tri, const Ray& ray, double tMax, SurfaceHit& hit) const {
    const auto& t = mesh_.tris[tri];
    const Vec3& a = mesh_.verts[t[0]];
    const Vec3 e1 = mesh_.verts[t[1]] - a;
    const Vec3 e2 = mesh_.verts[t[2]] - a;
    const Vec3 p = cross(ray.dir, e2);
    const double det = dot(e1, p);
    if (std::fabs(det) < 1e-20) return false;
    const double inv = 1.0 / det;
    const Vec3 s = ray.origin - a;
    const double u = dot(s, p) * inv;
    if (u < -1e-9 || u > 1.0 + 1e-9) return false;
    const Vec3 q = cross(s, e1);
    const double v = dot(ray.dir, q) * inv;
    if (v < -1e-9 || u + v > 1.0 + 1e-9) return false;
    const double dist = dot(e2, q) * inv;
    if (dist < 0.0 || dist > tMax) return false;
    hit.tri = tri;
    hit.t = dist;
    hit.u = clamp(u, 0.0, 1.0);
    hit.v = clamp(v, 0.0, 1.0 - hit.u);
    return true;
}

bool Surface::raycast(const Ray& rayIn, SurfaceHit& hit, double tMax) const {
    hit = SurfaceHit();
    if (nodes_.empty()) return false;
    Ray ray(rayIn.origin, normalize(rayIn.dir));
    const Vec3 invDir(1.0 / ray.dir.x, 1.0 / ray.dir.y, 1.0 / ray.dir.z);

    double best = tMax;
    SurfaceHit tmp;
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& node = nodes_[stack[--sp]];
        double tNear;
        if (!node.box.intersect(ray, invDir, best, tNear)) continue;
        if (node.count > 0) {
            for (int i = node.first; i < node.first + node.count; ++i) {
                if (rayTri(triOrder_[i], ray, best, tmp)) {
                    best = tmp.t;
                    hit = tmp;
                }
            }
        } else {
            // Push the farther child first so the nearer one is processed next.
            double tl, tr;
            const bool hl = nodes_[node.left].box.intersect(ray, invDir, best, tl);
            const bool hr = nodes_[node.right].box.intersect(ray, invDir, best, tr);
            if (hl && hr) {
                if (tl < tr) { stack[sp++] = node.right; stack[sp++] = node.left; }
                else         { stack[sp++] = node.left;  stack[sp++] = node.right; }
            } else if (hl) {
                stack[sp++] = node.left;
            } else if (hr) {
                stack[sp++] = node.right;
            }
        }
    }
    if (hit.tri < 0) return false;
    fillHit(hit);
    return true;
}

// ---------------------------------------------------------------------------
// Closest point (Ericson, Real-Time Collision Detection 5.1.5)
// ---------------------------------------------------------------------------
Vec3 Surface::closestOnTri(int tri, const Vec3& p, double& u, double& v) const {
    const auto& t = mesh_.tris[tri];
    const Vec3& a = mesh_.verts[t[0]];
    const Vec3& b = mesh_.verts[t[1]];
    const Vec3& c = mesh_.verts[t[2]];
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) { u = 0; v = 0; return a; }
    const Vec3 bp = p - b;
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) { u = 1; v = 0; return b; }
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const double w = d1 / (d1 - d3);
        u = w; v = 0;
        return a + ab * w;
    }
    const Vec3 cp = p - c;
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) { u = 0; v = 1; return c; }
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const double w = d2 / (d2 - d6);
        u = 0; v = w;
        return a + ac * w;
    }
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        u = 1.0 - w; v = w;
        return b + (c - b) * w;
    }
    const double denom = 1.0 / (va + vb + vc);
    v = vc * denom;  // weight of c
    u = vb * denom;  // weight of b
    return a + ab * u + ac * v;
}

bool Surface::closestPoint(const Vec3& p, SurfaceHit& hit, double maxDist) const {
    hit = SurfaceHit();
    if (nodes_.empty()) return false;
    double best2 = std::isinf(maxDist) ? kInf : maxDist * maxDist;
    int stack[128];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& node = nodes_[stack[--sp]];
        if (node.box.distance2(p) >= best2) continue;
        if (node.count > 0) {
            for (int i = node.first; i < node.first + node.count; ++i) {
                const int tri = triOrder_[i];
                if (triBoxes_[tri].distance2(p) >= best2) continue;
                double u, v;
                const Vec3 q = closestOnTri(tri, p, u, v);
                const double d2 = distance2(p, q);
                if (d2 < best2) {
                    best2 = d2;
                    hit.tri = tri;
                    hit.u = u;
                    hit.v = v;
                }
            }
        } else {
            const double dl = nodes_[node.left].box.distance2(p);
            const double dr = nodes_[node.right].box.distance2(p);
            if (dl < dr) { stack[sp++] = node.right; stack[sp++] = node.left; }
            else         { stack[sp++] = node.left;  stack[sp++] = node.right; }
        }
    }
    if (hit.tri < 0) return false;
    hit.t = std::sqrt(best2);
    fillHit(hit);
    return true;
}

bool Surface::project(const Vec3& p, double offset, Vec3& out, const Vec3* dirHint,
                      double searchDist) const {
    SurfaceHit best;
    if (dirHint && length2(*dirHint) > kEps) {
        // Shoot both ways along the hint and keep the nearer hit. This keeps
        // points on the intended sheet when the closest point would jump to a
        // neighbouring fold (fingers, lips, armpits...).
        const Vec3 d = normalize(*dirHint);
        SurfaceHit h1, h2;
        const bool ok1 = raycast(Ray(p, d), h1, searchDist);
        const bool ok2 = raycast(Ray(p, -d), h2, searchDist);
        if (ok1 && (!ok2 || h1.t <= h2.t)) best = h1;
        else if (ok2) best = h2;
        // A hit much farther than the closest surface point means the ray
        // grazed past the local sheet; prefer the closest point in that case.
        if (best.valid) {
            SurfaceHit cp;
            if (closestPoint(p, cp, best.t) && cp.t < best.t * 0.5) best = cp;
        }
    }
    if (!best.valid && !closestPoint(p, best, searchDist)) return false;
    out = best.point + best.normal * offset;
    return true;
}

// ---------------------------------------------------------------------------
// Plane slicing
// ---------------------------------------------------------------------------
std::vector<SliceLoop> Surface::slice(const Plane& plane) const {
    std::vector<SliceLoop> loops;
    if (mesh_.tris.empty()) return loops;

    const int nv = static_cast<int>(mesh_.verts.size());
    std::vector<double> dist(nv);
    for (int i = 0; i < nv; ++i) {
        const double d = plane.signedDistance(mesh_.verts[i]);
        // Vertices exactly on the plane are nudged to the positive side so that
        // every crossing is a proper edge crossing (no vertex special cases).
        dist[i] = (d == 0.0) ? 1e-300 : d;
    }

    std::unordered_map<uint64_t, int> pointOfEdge;
    std::vector<Vec3> points;
    std::vector<std::vector<int>> adj;

    auto pointFor = [&](int a, int b) {
        const uint64_t key = edgeKey(a, b);
        auto it = pointOfEdge.find(key);
        if (it != pointOfEdge.end()) return it->second;
        const double t = dist[a] / (dist[a] - dist[b]);
        const int idx = static_cast<int>(points.size());
        points.push_back(lerp(mesh_.verts[a], mesh_.verts[b], t));
        adj.emplace_back();
        pointOfEdge.emplace(key, idx);
        return idx;
    };

    for (const auto& t : mesh_.tris) {
        int crossing[2];
        int n = 0;
        for (int k = 0; k < 3 && n < 2; ++k) {
            const int a = t[k], b = t[(k + 1) % 3];
            if ((dist[a] > 0) != (dist[b] > 0)) crossing[n++] = pointFor(a, b);
        }
        if (n == 2 && crossing[0] != crossing[1]) {
            adj[crossing[0]].push_back(crossing[1]);
            adj[crossing[1]].push_back(crossing[0]);
        }
    }

    std::vector<char> used(points.size(), 0);
    auto walk = [&](int start) {
        SliceLoop loop;
        int prev = -1, cur = start;
        while (cur >= 0 && !used[cur]) {
            used[cur] = 1;
            loop.points.push_back(points[cur]);
            int next = -1;
            for (int nb : adj[cur])
                if (nb != prev && !used[nb]) { next = nb; break; }
            if (next < 0) {
                for (int nb : adj[cur])
                    if (nb == start && nb != prev && loop.points.size() > 2) loop.closed = true;
            }
            prev = cur;
            cur = next;
        }
        if (loop.points.size() >= 2) loops.push_back(std::move(loop));
    };

    // Open chains start at degree-1 points; whatever remains is closed loops.
    for (size_t i = 0; i < points.size(); ++i)
        if (!used[i] && adj[i].size() == 1) walk(static_cast<int>(i));
    for (size_t i = 0; i < points.size(); ++i)
        if (!used[i] && !adj[i].empty()) walk(static_cast<int>(i));
    return loops;
}

}  // namespace rtp
