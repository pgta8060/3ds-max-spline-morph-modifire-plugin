#include "Shapes.h"
#include "Test.h"
#include "rtp/Surface.h"

#include <random>

using namespace rtp;

namespace {
bool bruteRay(const TriMesh& m, const Ray& r, double& best) {
    best = kInf;
    for (const auto& t : m.tris) {
        const Vec3 a = m.verts[t[0]], e1 = m.verts[t[1]] - a, e2 = m.verts[t[2]] - a;
        const Vec3 p = cross(r.dir, e2);
        const double det = dot(e1, p);
        if (std::fabs(det) < 1e-20) continue;
        const Vec3 s = r.origin - a;
        const double u = dot(s, p) / det;
        if (u < 0 || u > 1) continue;
        const Vec3 q = cross(s, e1);
        const double v = dot(r.dir, q) / det;
        if (v < 0 || u + v > 1) continue;
        const double d = dot(e2, q) / det;
        if (d >= 0 && d < best) best = d;
    }
    return best < kInf;
}
}  // namespace

TEST(surface_raycast_matches_bruteforce) {
    const TriMesh mesh = kit::makeCharacter();
    const Surface s(mesh);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> U(-1, 1);
    int agree = 0, total = 0;
    for (int i = 0; i < 300; ++i) {
        const Vec3 o(U(rng) * 12, -12, U(rng) * 8);
        const Vec3 target(U(rng) * 4, U(rng), U(rng) * 4);
        const Ray r(o, normalize(target - o));
        double brute;
        const bool hb = bruteRay(s.mesh(), r, brute);
        SurfaceHit h;
        const bool hs = s.raycast(r, h);
        ++total;
        if (hb == hs && (!hb || std::fabs(brute - h.t) < 1e-9)) ++agree;
    }
    CHECK(agree == total);
}

TEST(surface_sphere_queries) {
    const Surface s(kit::makeSphere(Vec3(0, 0, 0), 2.0, 96, 48));
    SurfaceHit h;
    REQUIRE(s.raycast(Ray(Vec3(0, -10, 0.3), Vec3(0, 1, 0)), h));
    CHECK_NEAR(length(h.point), 2.0, 0.01);
    CHECK(dot(h.normal, normalize(h.point)) > 0.999);

    std::mt19937 rng(3);
    std::uniform_real_distribution<double> U(-3, 3);
    double worst = 0;
    for (int i = 0; i < 200; ++i) {
        const Vec3 p(U(rng), U(rng), U(rng));
        if (length(p) < 0.2) continue;
        REQUIRE(s.closestPoint(p, h));
        worst = std::max(worst, std::fabs(h.t - std::fabs(length(p) - 2.0)));
    }
    CHECK(worst < 0.01);

    Vec3 q;
    REQUIRE(s.project(Vec3(0, 0, 3), 0.1, q));
    CHECK_NEAR(length(q), 2.1, 0.01);
}

TEST(surface_slice_cylinder) {
    const Surface s(kit::makeCylinder(1.5, -2, 2, 64, 16));
    const auto loops = s.slice(Plane(Vec3(0, 0, 1), Vec3(0, 0, 0.37)));
    REQUIRE(loops.size() == 1);
    CHECK(loops[0].closed);
    CHECK(loops[0].points.size() == 128);  // 64 vertical + 64 diagonal edges cut
    double worst = 0;
    for (const auto& p : loops[0].points) worst = std::max(worst, std::fabs(std::hypot(p.x, p.y) - 1.5));
    CHECK(worst < 0.01);

    // Slanted plane through two separate tubes gives two loops.
    TriMesh two = kit::makeTube(Vec3(-3, 0, -2), Vec3(-3, 0, 2), 0.5);
    two.append(kit::makeTube(Vec3(3, 0, -2), Vec3(3, 0, 2), 0.5));
    const Surface s2(two);
    CHECK(s2.slice(Plane(normalize(Vec3(0.1, 0, 1)), Vec3(0, 0, 0))).size() == 2);
}
