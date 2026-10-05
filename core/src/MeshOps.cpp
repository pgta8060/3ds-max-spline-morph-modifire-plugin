#include "rtp/MeshOps.h"

#include "rtp/Surface.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace rtp {

namespace {

// Uniform spatial hash for neighbourhood queries on vertices.
class VertexGrid {
public:
    VertexGrid(const std::vector<Vec3>& pts, double cell) : pts_(pts), cell_(std::max(cell, 1e-9)) {
        for (int i = 0; i < static_cast<int>(pts.size()); ++i) cells_[keyOf(pts[i])].push_back(i);
    }
    template <class F>
    void forEachNear(const Vec3& p, F&& fn) const {
        const long long cx = coord(p.x), cy = coord(p.y), cz = coord(p.z);
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dy = -1; dy <= 1; ++dy)
                for (long long dz = -1; dz <= 1; ++dz) {
                    auto it = cells_.find(pack(cx + dx, cy + dy, cz + dz));
                    if (it == cells_.end()) continue;
                    for (int i : it->second) fn(i);
                }
    }

private:
    long long coord(double v) const { return static_cast<long long>(std::floor(v / cell_)); }
    static uint64_t pack(long long x, long long y, long long z) {
        const uint64_t m = (1ull << 21) - 1;
        return ((static_cast<uint64_t>(x) & m) << 42) | ((static_cast<uint64_t>(y) & m) << 21) |
               (static_cast<uint64_t>(z) & m);
    }
    uint64_t keyOf(const Vec3& p) const { return pack(coord(p.x), coord(p.y), coord(p.z)); }

    const std::vector<Vec3>& pts_;
    double cell_;
    std::unordered_map<uint64_t, std::vector<int>> cells_;
};

std::vector<std::vector<int>> vertexFaces(const PolyMesh& mesh) {
    std::vector<std::vector<int>> vf(mesh.verts.size());
    for (int f = 0; f < mesh.faceCount(); ++f)
        for (int v : mesh.faces[f]) vf[v].push_back(f);
    return vf;
}

// Neighbours along border edges only.
std::vector<std::vector<int>> borderNeighbours(const PolyMesh& mesh) {
    std::vector<std::vector<int>> nb(mesh.verts.size());
    for (const auto& e : mesh.edges())
        if (e.faceCount == 1) {
            nb[e.a].push_back(e.b);
            nb[e.b].push_back(e.a);
        }
    return nb;
}

double falloff(double d, double r) {
    if (d >= r) return 0.0;
    const double x = 1.0 - (d * d) / (r * r);
    return x * x;
}

}  // namespace

// ---------------------------------------------------------------------------
int autoWeld(PolyMesh& mesh, const std::vector<int>& sources, double threshold,
             std::vector<int>* remapOut) {
    const int nv = mesh.vertexCount();
    std::vector<int> rep(nv);
    std::iota(rep.begin(), rep.end(), 0);
    auto find = [&](int v) {
        while (rep[v] != v) v = rep[v] = rep[rep[v]];
        return v;
    };

    int merged = 0;
    if (threshold > 0 && !sources.empty()) {
        const auto vf = vertexFaces(mesh);
        const auto frozenV = mesh.frozenVertexMask();
        std::vector<char> isSource(nv, 0);
        for (int s : sources)
            if (s >= 0 && s < nv) isSource[s] = 1;
        std::vector<char> used(nv, 0);
        for (const auto& f : mesh.faces)
            for (int v : f) used[v] = 1;

        VertexGrid grid(mesh.verts, threshold);
        const double t2 = threshold * threshold;
        for (int s : sources) {
            if (s < 0 || s >= nv || find(s) != s || frozenV[s]) continue;
            int best = -1;
            double bestD2 = t2;
            bool bestIsExisting = false;
            grid.forEachNear(mesh.verts[s], [&](int c) {
                if (c == s || !used[c]) return;
                const int rc = find(c);
                if (rc == s) return;
                const double d2 = distance2(mesh.verts[s], mesh.verts[c]);
                if (d2 > t2) return;
                // Existing (non-source) vertices win over freshly created ones.
                const bool existing = !isSource[c];
                if ((existing && !bestIsExisting) || (existing == bestIsExisting && d2 < bestD2)) {
                    best = rc;
                    bestD2 = d2;
                    bestIsExisting = existing;
                }
            });
            if (best < 0) continue;
            // Reject merges that would collapse an edge or face.
            bool collapses = false;
            for (int f : vf[s]) {
                for (int v : mesh.faces[f])
                    if (v != s && find(v) == best) { collapses = true; break; }
                if (collapses) break;
            }
            if (collapses) continue;
            rep[s] = best;
            ++merged;
        }
    }

    for (auto& face : mesh.faces)
        for (int& v : face) v = find(v);
    if (merged > 0) {
        // Drop repeated consecutive indices introduced by merging.
        for (auto& face : mesh.faces) {
            std::vector<int> f2;
            for (int v : face)
                if (f2.empty() || f2.back() != v) f2.push_back(v);
            while (f2.size() > 1 && f2.front() == f2.back()) f2.pop_back();
            face.swap(f2);
        }
    }
    // Remove only the merged vertices; every other vertex keeps its relative
    // order (isolated vertices authored by the user are left alone).
    std::vector<int> keepMap(nv, -1);
    std::vector<Vec3> kept;
    kept.reserve(nv);
    for (int v = 0; v < nv; ++v) {
        if (rep[v] != v) continue;
        keepMap[v] = static_cast<int>(kept.size());
        kept.push_back(mesh.verts[v]);
    }
    for (auto& face : mesh.faces)
        for (int& v : face) v = keepMap[v];
    mesh.verts.swap(kept);
    if (remapOut) {
        remapOut->assign(nv, -1);
        for (int v = 0; v < nv; ++v) (*remapOut)[v] = keepMap[find(v)];
    }
    return merged;
}

// ---------------------------------------------------------------------------
int conformVertices(PolyMesh& mesh, const Surface& surface, double offset,
                    const std::vector<int>& subset) {
    if (surface.empty()) return 0;
    const auto normals = mesh.vertexNormals();
    const auto frozenV = mesh.frozenVertexMask();
    const double search = surface.scale() * 0.25;
    int moved = 0;
    auto doOne = [&](int v) {
        if (v < 0 || v >= mesh.vertexCount() || frozenV[v]) return;
        Vec3 q;
        const Vec3* hint = length2(normals[v]) > 0.5 ? &normals[v] : nullptr;
        if (surface.project(mesh.verts[v], offset, q, hint, search) ||
            surface.project(mesh.verts[v], offset, q)) {
            mesh.verts[v] = q;
            ++moved;
        }
    };
    if (subset.empty())
        for (int v = 0; v < mesh.vertexCount(); ++v) doOne(v);
    else
        for (int v : subset) doOne(v);
    return moved;
}

int relaxVertices(PolyMesh& mesh, const Surface* surface, const RelaxOptions& opt,
                  const std::vector<int>& subsetIn, const std::vector<double>& weights) {
    const int nv = mesh.vertexCount();
    if (nv == 0) return 0;
    std::vector<int> subset = subsetIn;
    if (subset.empty()) {
        subset.resize(nv);
        std::iota(subset.begin(), subset.end(), 0);
    }
    const auto nbs = mesh.vertexNeighbours();
    const auto bnbs = borderNeighbours(mesh);
    const auto isBorder = mesh.borderVertexMask();
    const auto frozenV = mesh.frozenVertexMask();

    std::vector<Vec3> next(mesh.verts);
    int moved = 0;
    for (int it = 0; it < opt.iterations; ++it) {
        const auto normals = mesh.vertexNormals();
        for (size_t k = 0; k < subset.size(); ++k) {
            const int v = subset[k];
            if (v < 0 || v >= nv || frozenV[v] || nbs[v].empty()) continue;
            const double w = (weights.empty() ? 1.0 : weights[k]) * opt.strength;
            if (w <= 0) continue;
            const Vec3& p = mesh.verts[v];
            Vec3 delta;
            if (isBorder[v]) {
                if (opt.keepBorders) {
                    if (bnbs[v].size() != 2) continue;
                    const Vec3 a = mesh.verts[bnbs[v][0]], b = mesh.verts[bnbs[v][1]];
                    // Corners stay pinned: sliding them would round off the patch.
                    const double c = dot(normalize(a - p), normalize(b - p));
                    if (c > -0.7) continue;
                    const Vec3 tangent = normalize(b - a);
                    delta = tangent * dot((a + b) * 0.5 - p, tangent);
                } else {
                    Vec3 avg;
                    for (int n : nbs[v]) avg += mesh.verts[n];
                    delta = avg / static_cast<double>(nbs[v].size()) - p;
                }
            } else {
                Vec3 avg;
                for (int n : nbs[v]) avg += mesh.verts[n];
                delta = projectOnPlane(avg / static_cast<double>(nbs[v].size()) - p, normals[v]);
            }
            next[v] = p + delta * w;
        }
        for (int v : subset)
            if (v >= 0 && v < nv) mesh.verts[v] = next[v];
        if (opt.reproject && surface && !surface->empty()) {
            for (int v : subset) {
                if (v < 0 || v >= nv || frozenV[v]) continue;
                Vec3 q;
                if (surface->project(mesh.verts[v], opt.offset, q)) mesh.verts[v] = q;
                next[v] = mesh.verts[v];
            }
        }
        moved = static_cast<int>(subset.size());
    }
    return moved;
}

// ---------------------------------------------------------------------------
std::vector<BrushSample> brushFootprint(const PolyMesh& mesh, const Vec3& center, double radius) {
    std::vector<BrushSample> out;
    const auto frozenV = mesh.frozenVertexMask();
    for (int v = 0; v < mesh.vertexCount(); ++v) {
        if (frozenV[v]) continue;
        const double w = falloff(distance(mesh.verts[v], center), radius);
        if (w > 0) out.push_back({v, w});
    }
    return out;
}

void moveBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
               double radius, const Vec3& delta, double strength) {
    const auto fp = brushFootprint(mesh, center, radius);
    std::vector<int> moved;
    for (const auto& s : fp) {
        mesh.verts[s.vertex] += delta * (s.weight * strength);
        moved.push_back(s.vertex);
    }
    conformVertices(mesh, surface, offset, moved);
}

void relaxBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
                double radius, double strength) {
    const auto fp = brushFootprint(mesh, center, radius);
    if (fp.empty()) return;
    std::vector<int> subset;
    std::vector<double> weights;
    for (const auto& s : fp) {
        subset.push_back(s.vertex);
        weights.push_back(s.weight);
    }
    RelaxOptions opt;
    opt.iterations = 1;
    opt.strength = strength;
    opt.offset = offset;
    relaxVertices(mesh, &surface, opt, subset, weights);
}

void conformBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
                  double radius) {
    const auto fp = brushFootprint(mesh, center, radius);
    std::vector<int> subset;
    for (const auto& s : fp) subset.push_back(s.vertex);
    conformVertices(mesh, surface, offset, subset);
}

// ---------------------------------------------------------------------------
int snapToPlane(PolyMesh& mesh, int axis, double value, double threshold) {
    const auto frozenV = mesh.frozenVertexMask();
    int n = 0;
    for (int v = 0; v < mesh.vertexCount(); ++v) {
        if (frozenV[v]) continue;
        double& c = mesh.verts[v][axis];
        if (std::fabs(c - value) <= threshold && c != value) {
            c = value;
            ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
bool pickBorderSpan(const PolyMesh& mesh, const Polyline& stroke, double tolerance,
                    BorderChain& out) {
    if (!stroke.valid() && stroke.pts.size() != 1) return false;
    const auto loops = mesh.borderLoops();
    if (loops.empty()) return false;

    const bool isClick = stroke.pts.size() < 2 || stroke.length() < tolerance * 2.0;
    int bestLoop = -1;
    int bestCount = 0;
    double bestDist = kInf;
    std::vector<char> bestNear;
    for (int li = 0; li < static_cast<int>(loops.size()); ++li) {
        const auto& loop = loops[li];
        std::vector<char> nearMask(loop.verts.size(), 0);
        int count = 0;
        double minD = kInf;
        for (size_t i = 0; i < loop.verts.size(); ++i) {
            double d;
            if (stroke.pts.size() < 2) d = distance(stroke.pts[0], mesh.verts[loop.verts[i]]);
            else stroke.closestParam(mesh.verts[loop.verts[i]], &d);
            minD = std::min(minD, d);
            if (d <= tolerance) { nearMask[i] = 1; ++count; }
        }
        const bool better = isClick ? minD < bestDist
                                    : (count > bestCount || (count == bestCount && minD < bestDist));
        if (better && (isClick ? minD <= tolerance : count >= 2)) {
            bestLoop = li;
            bestCount = count;
            bestDist = minD;
            bestNear = nearMask;
        }
    }
    if (bestLoop < 0) return false;
    const auto& loop = loops[bestLoop];
    if (isClick) {
        out = loop;
        return true;
    }
    const int n = static_cast<int>(loop.verts.size());
    if (bestCount == n && loop.closed) {
        out = loop;
        return true;
    }
    // Longest contiguous run (wrapping around for closed loops).
    int bestStart = -1, bestLen = 0;
    for (int s = 0; s < n; ++s) {
        if (!bestNear[s]) continue;
        const int prev = (s - 1 + n) % n;
        if (loop.closed && bestNear[prev] && bestCount < n) continue;  // not a run start
        if (!loop.closed && s > 0 && bestNear[s - 1]) continue;
        int len = 0;
        while (len < n && bestNear[(s + len) % n] && (loop.closed || s + len < n)) ++len;
        if (len > bestLen) { bestLen = len; bestStart = s; }
    }
    if (bestLen < 2) return false;
    out.verts.clear();
    out.closed = false;
    for (int k = 0; k < bestLen; ++k) out.verts.push_back(loop.verts[(bestStart + k) % n]);
    return true;
}

Polyline chainToPolyline(const PolyMesh& mesh, const BorderChain& chain) {
    Polyline pl;
    pl.closed = chain.closed;
    for (int v : chain.verts) pl.pts.push_back(mesh.verts[v]);
    return pl;
}

}  // namespace rtp
