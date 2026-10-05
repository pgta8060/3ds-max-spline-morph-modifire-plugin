#include "Shapes.h"
#include "Test.h"
#include "rtp/MeshOps.h"
#include "rtp/PolyMesh.h"
#include "rtp/Surface.h"

using namespace rtp;

namespace {
// nx x ny quad grid in the XY plane at height z.
PolyMesh grid(int nx, int ny, double size = 1.0, double z = 0.0, Vec3 origin = Vec3()) {
    PolyMesh m;
    for (int y = 0; y <= ny; ++y)
        for (int x = 0; x <= nx; ++x) m.addVertex(origin + Vec3(x * size, y * size, z));
    for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) {
            const int a = y * (nx + 1) + x;
            m.addFace({a, a + 1, a + nx + 2, a + nx + 1});
        }
    return m;
}
}  // namespace

TEST(polymesh_inspect_grid) {
    const PolyMesh m = grid(4, 3);
    const TopologyStats s = m.inspect();
    CHECK(s.vertices == 20);
    CHECK(s.faces == 12);
    CHECK(s.quads == 12);
    CHECK(s.edges == 31);
    CHECK(s.borderEdges == 14);
    CHECK(s.borderLoops == 1);
    CHECK(s.degenerateFaces == 0);
    const auto loops = m.borderLoops();
    REQUIRE(loops.size() == 1);
    CHECK(loops[0].closed);
    CHECK(loops[0].verts.size() == 14);
}

TEST(polymesh_repair) {
    PolyMesh m = grid(2, 1);
    m.addVertex(Vec3(9, 9, 9));          // isolated
    m.addFace({0, 1, 1});                // degenerate
    m.addFace({0, 0, 1, 4});             // repeated index -> becomes a triangle
    const int fixes = m.repair();
    CHECK(fixes >= 3);
    const TopologyStats s = m.inspect();
    CHECK(s.isolatedVertices == 0);
    CHECK(s.degenerateFaces == 0);
}

TEST(autoweld_is_operation_scoped) {
    // Two grids with a coincident seam; only the second grid's vertices are sources.
    PolyMesh m = grid(3, 1);
    const int base = m.vertexCount();
    PolyMesh g2 = grid(3, 1, 1.0, 0.0, Vec3(0, 1.0001, 0));
    for (const auto& v : g2.verts) m.addVertex(v);
    for (auto f : g2.faces) {
        for (int& v : f) v += base;
        m.addFace(f);
    }
    std::vector<int> sources;
    for (int v = base; v < m.vertexCount(); ++v) sources.push_back(v);
    const Vec3 untouched = m.verts[5];
    std::vector<int> remap;
    const int merged = autoWeld(m, sources, 0.01, &remap);
    CHECK(merged == 4);
    CHECK(m.vertexCount() == 12);
    CHECK(m.inspect().borderLoops == 1);
    CHECK(distance(m.verts[remap[5]], untouched) == 0.0);  // existing vertices never move
}

TEST(pick_border_span) {
    const PolyMesh m = grid(6, 2);
    // Stroke along the top edge (y = 2), covering x in [1, 4].
    Polyline stroke({Vec3(0.9, 2.02, 0), Vec3(4.1, 2.02, 0)}, false);
    BorderChain span;
    REQUIRE(pickBorderSpan(m, stroke, 0.15, span));
    CHECK(!span.closed);
    CHECK(span.verts.size() == 4);
    for (int v : span.verts) CHECK_NEAR(m.verts[v].y, 2.0, 1e-12);

    // A click picks the complete loop.
    BorderChain loop;
    REQUIRE(pickBorderSpan(m, Polyline({Vec3(3, 0.01, 0)}, false), 0.1, loop));
    CHECK(loop.closed);
    CHECK(loop.verts.size() == 16);
}

TEST(relax_and_conform_on_sphere) {
    const Surface s(kit::makeSphere(Vec3(), 2.0, 96, 48));
    PolyMesh m = grid(6, 6, 0.3, 2.5, Vec3(-0.9, -0.9, 0));
    CHECK(conformVertices(m, s, 0.05) == m.vertexCount());
    double worst = 0;
    for (const auto& v : m.verts) worst = std::max(worst, std::fabs(length(v) - 2.05));
    CHECK(worst < 0.01);

    // Jitter one interior vertex, relax, and verify it moved back toward its neighbours.
    const int v = 3 * 7 + 3;
    const Vec3 orig = m.verts[v];
    m.verts[v] += Vec3(0.12, 0.0, 0.0);
    RelaxOptions opt;
    opt.iterations = 10;
    opt.offset = 0.05;
    relaxVertices(m, &s, opt);
    CHECK(distance(m.verts[v], orig) < 0.04);
    CHECK_NEAR(length(m.verts[v]), 2.05, 0.01);
}

TEST(snap_to_symmetry_plane) {
    PolyMesh m = grid(2, 2, 1.0, 0.0, Vec3(-1.003, 0, 0));
    CHECK(snapToPlane(m, 0, 0.0, 0.01) == 3);
    int onPlane = 0;
    for (const auto& v : m.verts) onPlane += v.x == 0.0;
    CHECK(onPlane == 3);
}
