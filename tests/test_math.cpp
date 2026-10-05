#include "Test.h"
#include "rtp/Math.h"
#include "rtp/MonotoneMap.h"

using namespace rtp;

TEST(math_vector_basics) {
    const Vec3 a(1, 2, 3), b(4, 5, 6);
    CHECK_NEAR(dot(a, b), 32.0, 1e-12);
    const Vec3 c = cross(Vec3(1, 0, 0), Vec3(0, 1, 0));
    CHECK_NEAR(c.z, 1.0, 1e-12);
    CHECK_NEAR(length(normalize(b)), 1.0, 1e-12);
    const Vec3 r = rotateAround(Vec3(1, 0, 0), Vec3(0, 0, 1), kPi / 2);
    CHECK_NEAR(r.x, 0.0, 1e-12);
    CHECK_NEAR(r.y, 1.0, 1e-12);
    CHECK_NEAR(dot(anyPerpendicular(normalize(a)), normalize(a)), 0.0, 1e-12);
}

TEST(math_aabb_ray) {
    Aabb box;
    box.expand(Vec3(-1, -1, -1));
    box.expand(Vec3(1, 1, 1));
    const Ray ray(Vec3(-5, 0.2, 0.3), Vec3(1, 0, 0));
    const Vec3 inv(1.0 / ray.dir.x, 1.0 / ray.dir.y, 1.0 / ray.dir.z);
    double t;
    CHECK(box.intersect(ray, inv, kInf, t));
    CHECK_NEAR(t, 4.0, 1e-12);
    const Ray miss(Vec3(-5, 3, 0), Vec3(1, 0, 0));
    CHECK(!box.intersect(miss, Vec3(1, kInf, kInf), kInf, t));
    CHECK_NEAR(box.distance2(Vec3(3, 0, 0)), 4.0, 1e-12);
}

TEST(monotone_map_open) {
    MonotoneMap m;
    m.setIdentity(false);
    CHECK_NEAR(m(0.25), 0.25, 1e-12);
    m.setAnchors({{0.5, 0.2}}, false);
    CHECK_NEAR(m(0.0), 0.0, 1e-12);
    CHECK_NEAR(m(0.5), 0.2, 1e-12);
    CHECK_NEAR(m(1.0), 1.0, 1e-12);
    double prev = -1;
    bool monotone = true;
    for (int i = 0; i <= 200; ++i) {
        const double v = m(i / 200.0);
        if (v < prev - 1e-12) monotone = false;
        prev = v;
    }
    CHECK(monotone);
    // Conflicting anchor (would fold the flow) is discarded.
    m.setAnchors({{0.3, 0.6}, {0.6, 0.4}}, false);
    CHECK_NEAR(m(0.3), 0.6, 1e-12);
    CHECK(m.anchorCount() == 3);
}

TEST(monotone_map_periodic) {
    MonotoneMap m;
    m.setAnchors({{0.25, 0.30}}, true);
    // A single anchor on a loop is a pure rotation of the start.
    CHECK_NEAR(m(0.25), 0.30, 1e-9);
    CHECK_NEAR(m(0.75), 0.80, 1e-9);
    m.setAnchors({{0.1, 0.95}, {0.6, 0.5}}, true);  // 0.95 unwraps to -0.05
    CHECK_NEAR(m(0.1), -0.05, 1e-9);
    CHECK_NEAR(m(0.6), 0.5, 1e-9);
    CHECK_NEAR(m(1.1), 0.95, 1e-9);
    double prev = -10;
    bool monotone = true;
    for (int i = 0; i <= 400; ++i) {
        const double v = m(i / 200.0);
        if (v < prev - 1e-12) monotone = false;
        prev = v;
    }
    CHECK(monotone);
}
