#include "Shapes.h"
#include "Test.h"
#include "rtp/Stroke.h"
#include "rtp/Surface.h"

#include <algorithm>

using namespace rtp;

namespace {
kit::Camera frontCamera() {
    kit::Camera cam;
    cam.eye = Vec3(0, -10, 0);
    cam.target = Vec3(0, 0, 0);
    return cam;
}
std::vector<std::pair<double, double>> screenSegment(const kit::Camera& cam, const Vec3& a, const Vec3& b,
                                                     int n = 32) {
    std::vector<std::pair<double, double>> pts;
    for (int i = 0; i <= n; ++i) {
        double x, y, z;
        cam.project(lerp(a, b, static_cast<double>(i) / n), x, y, z);
        pts.push_back({x, y});
    }
    return pts;
}
}  // namespace

TEST(stroke_on_sphere_follows_surface) {
    const Surface s(kit::makeSphere(Vec3(), 2.0, 96, 48));
    const auto cam = frontCamera();
    const auto rays = kit::strokeRays(cam, screenSegment(cam, Vec3(-1.2, -3, 0.3), Vec3(1.2, -3, -0.2)));
    StrokeOptions opt;
    opt.offset = 0.02;
    const StrokeResult r = strokeFromRays(rays, s, opt);
    REQUIRE(r.ok);
    CHECK(!r.curve.closed);
    CHECK(r.curve.size() >= 8);
    CHECK(r.misses == 0);
    double worst = 0;
    for (const auto& p : r.curve.pts) worst = std::max(worst, std::fabs(length(p) - 2.02));
    CHECK(worst < 0.01);
}

TEST(stroke_keeps_part_on_surface_and_detects_closure) {
    const Surface s(kit::makeSphere(Vec3(), 2.0, 96, 48));
    const auto cam = frontCamera();
    // Starts in empty space, ends on the sphere.
    const auto rays = kit::strokeRays(cam, screenSegment(cam, Vec3(-6, -3, 0), Vec3(0.5, -3, 0)));
    const StrokeResult r = strokeFromRays(rays, s, StrokeOptions());
    REQUIRE(r.ok);
    CHECK(r.misses > 0);
    CHECK(r.curve.pts.front().x > -2.05);

    // A circle drawn on screen produces a closed curve.
    std::vector<std::pair<double, double>> circle;
    double cx, cy, cz;
    cam.project(Vec3(0, 0, 0), cx, cy, cz);
    for (int i = 0; i <= 64; ++i) {
        const double a = 2 * kPi * i / 64.0 * 0.985;
        circle.push_back({cx + 90 * std::cos(a), cy + 90 * std::sin(a)});
    }
    const StrokeResult c = strokeFromRays(kit::strokeRays(cam, circle), s, StrokeOptions());
    REQUIRE(c.ok);
    CHECK(c.curve.closed);
}

TEST(contour_wraps_around_limb) {
    const Surface s(kit::makeTube(Vec3(-4, 0, 0), Vec3(4, 0, 0), 1.0, 96, 48));
    const auto cam = frontCamera();
    // Drag across the tube (top to bottom), well past both silhouettes.
    const auto rays = kit::strokeRays(cam, screenSegment(cam, Vec3(0.7, -2, 2.0), Vec3(0.7, -2, -2.0)));
    ContourOptions copt;
    copt.viewDir = cam.forward();
    const StrokeResult r = contourFromRays(rays, s, copt);
    REQUIRE(r.ok);
    CHECK(r.curve.closed);
    std::vector<double> angles;
    double worstR = 0, worstX = 0;
    for (const auto& p : r.curve.pts) {
        worstR = std::max(worstR, std::fabs(std::hypot(p.y, p.z) - 1.0));
        worstX = std::max(worstX, std::fabs(p.x - r.curve.pts[0].x));
        angles.push_back(std::atan2(p.z, p.y));
    }
    CHECK(worstR < 0.01);
    CHECK(worstX < 0.05);  // ring lies in a plane across the limb
    std::sort(angles.begin(), angles.end());
    double maxGap = 2 * kPi - (angles.back() - angles.front());
    for (size_t i = 1; i < angles.size(); ++i) maxGap = std::max(maxGap, angles[i] - angles[i - 1]);
    CHECK(maxGap < 0.3);  // covers the hidden back side too
}
