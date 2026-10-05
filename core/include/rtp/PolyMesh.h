// TopoWeave core - authored retopology mesh (quads / n-gons).
#pragma once

#include "rtp/Math.h"

#include <string>
#include <vector>

namespace rtp {

class Surface;

// An ordered run of boundary vertices. `closed` means the run is a complete
// border loop (e.g. the open end of a tube); otherwise it is a span of a loop.
struct BorderChain {
    std::vector<int> verts;
    bool closed = false;
    int edgeCount() const {
        const int n = static_cast<int>(verts.size());
        return closed ? n : n - 1;
    }
};

struct TopologyStats {
    int vertices = 0;
    int edges = 0;
    int faces = 0;
    int quads = 0;
    int triangles = 0;
    int ngons = 0;
    int isolatedVertices = 0;
    int borderEdges = 0;
    int nonManifoldEdges = 0;
    int degenerateFaces = 0;
    int borderLoops = 0;
    int frozenFaces = 0;

    std::string report() const;
};

struct MeshEdge {
    int a = -1, b = -1;   // a < b
    int f0 = -1, f1 = -1; // adjacent faces (f1 == -1 on a border)
    int faceCount = 0;    // > 2 means non-manifold
};

class PolyMesh {
public:
    std::vector<Vec3> verts;
    std::vector<std::vector<int>> faces;
    std::vector<char> frozen;  // per face; frozen faces are protected from edits

    bool empty() const { return faces.empty() && verts.empty(); }
    int vertexCount() const { return static_cast<int>(verts.size()); }
    int faceCount() const { return static_cast<int>(faces.size()); }

    int addVertex(const Vec3& p);
    int addFace(std::vector<int> face);
    bool isFrozen(int face) const { return face < static_cast<int>(frozen.size()) && frozen[face]; }

    Aabb bounds() const;
    std::vector<MeshEdge> edges() const;
    Vec3 faceNormal(int face) const;   // Newell normal (works for n-gons)
    Vec3 faceCenter(int face) const;
    std::vector<Vec3> vertexNormals() const;
    // vertex -> neighbouring vertices (edge connected)
    std::vector<std::vector<int>> vertexNeighbours() const;
    // per vertex: true if the vertex lies on a border edge
    std::vector<char> borderVertexMask() const;
    // per vertex: true if any adjacent face is frozen
    std::vector<char> frozenVertexMask() const;

    // Complete border loops following the face winding direction.
    std::vector<BorderChain> borderLoops() const;

    TopologyStats inspect() const;

    // Removes unused vertices; optional remap old->new (-1 for removed).
    void compact(std::vector<int>* remap = nullptr);
    // Removes repeated vertex indices inside faces, faces with < 3 distinct
    // vertices, zero-area faces and unused vertices. Returns number of fixes.
    int repair();
    // Flips every face whose normal disagrees with the reference surface.
    int orientToSurface(const Surface& surface);
};

}  // namespace rtp
