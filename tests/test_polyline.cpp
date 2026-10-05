#include "Test.h"
#include "rtp/Polyline.h"

using namespace rtp;

namespace {
Polyline circle(double r, int n, double phase = 0.0, double z = 0.0) {
    Polyline p;
    p.closed = true;
    for (int i = 0; i < n; ++i) {
        const double a = phase + 2 * kPi * i / n;
        p.pts.push_back(Vec3(r * std::cos(a), r * std::sin(a), z));
    }
    return p;
}
}  // namespace

TEST(polyline_length_and_resample) {
    Polyline line({Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 3, 0)}, false);
    CHECK_NEAR(line.length(), 4.0, 1e-12);
    const Polyline r = line.resampled(9);
    REQUIRE(r.size() == 9);
    for (int i = 1; i < r.size(); ++i) CHECK_NEAR(distance(r.pts[i - 1], r.pts[i]), 0.5, 1e-9);
    CHECK_NEAR(distance(r.pts.back(), Vec3(1, 3, 0)), 0.0, 1e-12);

    const Polyline c = circle(1.0, 360).resampled(12);
    CHECK(c.closed);
    CHECK(c.size() == 12);
    CHECK_NEAR(distance(c.pts[0], c.pts[1]), distance(c.pts[11], c.pts[0]), 1e-3);
}

TEST(polyline_closest_and_rotate) {
    Polyline line({Vec3(0, 0, 0), Vec3(4, 0, 0)}, false);
    double d;
    CHECK_NEAR(line.closestParam(Vec3(1, 2, 0), &d), 0.25, 1e-12);
    CHECK_NEAR(d, 2.0, 1e-12);

    Polyline c = circle(1.0, 64);
    c.rotateStart(0.25);
    CHECK_NEAR(c.pts[0].x, 0.0, 1e-9);
    CHECK_NEAR(c.pts[0].y, 1.0, 1e-9);
    CHECK_NEAR(c.length(), circle(1.0, 64).length(), 1e-9);
}

TEST(polyline_align_closed_recovers_shift_and_winding) {
    const Polyline a = circle(1.0, 128);
    Polyline b = circle(1.1, 128, 0.9, 0.5);  // rotated start, offset ring
    b.reverse();                               // opposite winding
    alignClosed(a, b);
    // After alignment both rings start at the same angle and run the same way.
    CHECK_NEAR(std::atan2(b.pts[0].y, b.pts[0].x), 0.0, 0.03);
    const Vec3 ta = a.pts[1] - a.pts[0];
    const Vec3 tb = b.pts[1] - b.pts[0];
    CHECK(dot(ta, tb) > 0);
}

TEST(polyline_align_direction_open) {
    const Polyline a({Vec3(0, 0, 0), Vec3(10, 0, 0)}, false);
    Polyline b({Vec3(10, 1, 0), Vec3(0, 1, 0)}, false);
    CHECK(alignDirection(a, b));
    CHECK_NEAR(b.pts[0].x, 0.0, 1e-12);
}

TEST(polyline_closest_approach) {
    const Polyline a({Vec3(0, 0, 0), Vec3(10, 0, 0)}, false);
    const Polyline b({Vec3(3, -5, 0.2), Vec3(3, 5, 0.2)}, false);
    double pa, pb;
    const double d = closestApproach(a, b, pa, pb);
    CHECK_NEAR(d, 0.2, 1e-12);
    CHECK_NEAR(pa, 0.3, 1e-12);
    CHECK_NEAR(pb, 0.5, 1e-12);
}

TEST(catmull_rom_interpolates) {
    const Vec3 p0(0, 0, 0), p1(1, 1, 0), p2(2, 1, 0), p3(3, 0, 0);
    CHECK_NEAR(distance(catmullRom(p0, p1, p2, p3, 0.0), p1), 0.0, 1e-9);
    CHECK_NEAR(distance(catmullRom(p0, p1, p2, p3, 1.0), p2), 0.0, 1e-9);
    // Collinear control points stay on the line.
    const Vec3 m = catmullRom(Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(2, 0, 0), Vec3(3, 0, 0), 0.5);
    CHECK_NEAR(m.y, 0.0, 1e-12);
    CHECK_NEAR(m.x, 1.5, 1e-9);
}
