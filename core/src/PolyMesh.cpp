#include "rtp/PolyMesh.h"

#include "rtp/Surface.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace rtp {

namespace {
inline uint64_t key(int a, int b) {
    if (a > b) std::swap(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
}  // namespace

std::string TopologyStats::report() const {
    std::ostringstream s;
    s << "Vertices: " << vertices << "\n"
      << "Edges: " << edges << "\n"
      << "Faces: " << faces << "  (quads " << quads << ", triangles " << triangles << ", n-gons "
      << ngons << ")\n"
      << "Frozen faces: " << frozenFaces << "\n"
      << "Border edges: " << borderEdges << " in " << borderLoops << " loop(s)\n"
      << "Isolated vertices: " << isolatedVertices << "\n"
      << "Non-manifold edges: " << nonManifoldEdges << "\n"
      << "Degenerate faces: " << degenerateFaces << "\n";
    return s.str();
}

int PolyMesh::addVertex(const Vec3& p) {
    verts.push_back(p);
    return static_cast<int>(verts.size()) - 1;
}

int PolyMesh::addFace(std::vector<int> face) {
    faces.push_back(std::move(face));
    if (frozen.size() < faces.size()) frozen.resize(faces.size(), 0);
    return static_cast<int>(faces.size()) - 1;
}

Aabb PolyMesh::bounds() const {
    Aabb b;
    for (const auto& v : verts) b.expand(v);
    return b;
}

std::vector<MeshEdge> PolyMesh::edges() const {
    std::unordered_map<uint64_t, int> index;
    std::vector<MeshEdge> out;
    for (int f = 0; f < faceCount(); ++f) {
        const auto& face = faces[f];
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[i], b = face[(i + 1) % n];
            if (a == b) continue;
            const uint64_t k = key(a, b);
            auto it = index.find(k);
            if (it == index.end()) {
                MeshEdge e;
                e.a = std::min(a, b);
                e.b = std::max(a, b);
                e.f0 = f;
                e.faceCount = 1;
                index.emplace(k, static_cast<int>(out.size()));
                out.push_back(e);
            } else {
                MeshEdge& e = out[it->second];
                if (e.faceCount == 1) e.f1 = f;
                ++e.faceCount;
            }
        }
    }
    return out;
}

Vec3 PolyMesh::faceNormal(int f) const {
    const auto& face = faces[f];
    Vec3 n;
    const int k = static_cast<int>(face.size());
    for (int i = 0; i < k; ++i) {
        const Vec3& a = verts[face[i]];
        const Vec3& b = verts[face[(i + 1) % k]];
        n.x += (a.y - b.y) * (a.z + b.z);
        n.y += (a.z - b.z) * (a.x + b.x);
        n.z += (a.x - b.x) * (a.y + b.y);
    }
    return normalize(n);
}

Vec3 PolyMesh::faceCenter(int f) const {
    Vec3 c;
    for (int v : faces[f]) c += verts[v];
    return faces[f].empty() ? c : c / static_cast<double>(faces[f].size());
}

std::vector<Vec3> PolyMesh::vertexNormals() const {
    std::vector<Vec3> n(verts.size());
    for (int f = 0; f < faceCount(); ++f) {
        const Vec3 fn = faceNormal(f);
        for (int v : faces[f]) n[v] += fn;
    }
    for (auto& v : n) v = normalize(v);
    return n;
}

std::vector<std::vector<int>> PolyMesh::vertexNeighbours() const {
    std::vector<std::vector<int>> nb(verts.size());
    for (const auto& e : edges()) {
        nb[e.a].push_back(e.b);
        nb[e.b].push_back(e.a);
    }
    return nb;
}

std::vector<char> PolyMesh::borderVertexMask() const {
    std::vector<char> m(verts.size(), 0);
    for (const auto& e : edges())
        if (e.faceCount == 1) m[e.a] = m[e.b] = 1;
    return m;
}

std::vector<char> PolyMesh::frozenVertexMask() const {
    std::vector<char> m(verts.size(), 0);
    for (int f = 0; f < faceCount(); ++f)
        if (isFrozen(f))
            for (int v : faces[f]) m[v] = 1;
    return m;
}

std::vector<BorderChain> PolyMesh::borderLoops() const {
    // A border edge is used by exactly one face; we take it in that face's
    // winding direction so loops are consistently oriented.
    std::unordered_map<uint64_t, int> count;
    for (const auto& face : faces) {
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) ++count[key(face[i], face[(i + 1) % n])];
    }
    std::unordered_map<int, std::vector<int>> next;  // directed border edges a -> b
    for (const auto& face : faces) {
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[i], b = face[(i + 1) % n];
            if (a != b && count[key(a, b)] == 1) next[a].push_back(b);
        }
    }
    std::vector<BorderChain> loops;
    std::unordered_map<uint64_t, char> used;
    // Walk starting points in face order so the result is deterministic.
    std::vector<std::pair<int, int>> starts;
    for (const auto& face : faces) {
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[i], b = face[(i + 1) % n];
            if (a != b && count[key(a, b)] == 1) starts.emplace_back(a, b);
        }
    }
    for (const auto& sb : starts) {
        const int start = sb.first;
        if (used[key(start, sb.second)]) continue;
        BorderChain chain;
        int a = start, b = sb.second;
        chain.verts.push_back(a);
        while (true) {
            used[key(a, b)] = 1;
            if (b == start) { chain.closed = true; break; }
            chain.verts.push_back(b);
            auto it = next.find(b);
            int nb = -1;
            if (it != next.end())
                for (int c : it->second)
                    if (!used[key(b, c)]) { nb = c; break; }
            if (nb < 0) break;
            a = b;
            b = nb;
        }
        if (chain.verts.size() >= 2) loops.push_back(std::move(chain));
    }
    return loops;
}

TopologyStats PolyMesh::inspect() const {
    TopologyStats s;
    s.vertices = vertexCount();
    s.faces = faceCount();
    std::vector<char> used(verts.size(), 0);
    for (int f = 0; f < faceCount(); ++f) {
        const auto& face = faces[f];
        const size_t n = face.size();
        if (n == 3) ++s.triangles;
        else if (n == 4) ++s.quads;
        else if (n > 4) ++s.ngons;
        if (isFrozen(f)) ++s.frozenFaces;
        std::vector<int> sorted(face);
        std::sort(sorted.begin(), sorted.end());
        const bool repeated = std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end();
        bool zeroArea = false;
        if (n >= 3 && !repeated) {
            Vec3 c;
            for (int v : face) c += verts[v];
            c /= static_cast<double>(n);
            double area2 = 0;
            for (size_t i = 0; i < n; ++i)
                area2 += length(cross(verts[face[i]] - c, verts[face[(i + 1) % n]] - c));
            zeroArea = area2 <= 1e-14;
        }
        if (n < 3 || repeated || zeroArea) ++s.degenerateFaces;
        for (int v : face) used[v] = 1;
    }
    for (char u : used) if (!u) ++s.isolatedVertices;
    for (const auto& e : edges()) {
        ++s.edges;
        if (e.faceCount == 1) ++s.borderEdges;
        if (e.faceCount > 2) ++s.nonManifoldEdges;
    }
    s.borderLoops = static_cast<int>(borderLoops().size());
    return s;
}

void PolyMesh::compact(std::vector<int>* remapOut) {
    // Keeps the relative order of surviving vertices stable.
    std::vector<int> remap(verts.size(), -1);
    std::vector<Vec3> nv;
    nv.reserve(verts.size());
    std::vector<char> used(verts.size(), 0);
    for (const auto& face : faces)
        for (int v : face) used[v] = 1;
    int next = 0;
    for (size_t i = 0; i < verts.size(); ++i) {
        if (used[i]) {
            remap[i] = next++;
            nv.push_back(verts[i]);
        } else {
            remap[i] = -1;
        }
    }
    for (auto& face : faces)
        for (int& v : face) v = remap[v];
    verts.swap(nv);
    if (remapOut) *remapOut = std::move(remap);
}

int PolyMesh::repair() {
    int fixes = 0;
    std::vector<std::vector<int>> nf;
    std::vector<char> nfrozen;
    for (int f = 0; f < faceCount(); ++f) {
        std::vector<int> face;
        for (int v : faces[f])
            if (face.empty() || face.back() != v) face.push_back(v);
        while (face.size() > 1 && face.front() == face.back()) face.pop_back();
        if (face.size() != faces[f].size()) ++fixes;
        std::vector<int> sorted(face);
        std::sort(sorted.begin(), sorted.end());
        const bool repeated = std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end();
        bool ok = face.size() >= 3 && !repeated;
        if (ok) {
            Vec3 c;
            for (int v : face) c += verts[v];
            c /= static_cast<double>(face.size());
            double area2 = 0;
            for (size_t i = 0; i < face.size(); ++i)
                area2 += length(cross(verts[face[i]] - c, verts[face[(i + 1) % face.size()]] - c));
            ok = area2 > 1e-14;
        }
        if (!ok) { ++fixes; continue; }
        nf.push_back(std::move(face));
        nfrozen.push_back(isFrozen(f) ? 1 : 0);
    }
    faces.swap(nf);
    frozen.swap(nfrozen);
    const size_t before = verts.size();
    compact();
    fixes += static_cast<int>(before - verts.size());
    return fixes;
}

int PolyMesh::orientToSurface(const Surface& surface) {
    int flipped = 0;
    for (int f = 0; f < faceCount(); ++f) {
        SurfaceHit hit;
        if (!surface.closestPoint(faceCenter(f), hit)) continue;
        if (dot(faceNormal(f), hit.normal) < 0) {
            std::reverse(faces[f].begin(), faces[f].end());
            ++flipped;
        }
    }
    return flipped;
}

}  // namespace rtp
