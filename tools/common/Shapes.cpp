#include "Shapes.h"

#include <cmath>
#include <functional>

namespace rtp {
namespace kit {

namespace {
// Grid of (segments x (rings+1)) vertices wrapped in u; caps optional.
TriMesh wrappedGrid(int segments, int rings, const std::function<Vec3(double, double)>& f) {
    TriMesh m;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s < segments; ++s)
            m.verts.push_back(f(static_cast<double>(s) / segments, static_cast<double>(r) / rings));
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            const int a = r * segments + s;
            const int b = r * segments + (s + 1) % segments;
            const int c = (r + 1) * segments + (s + 1) % segments;
            const int d = (r + 1) * segments + s;
            m.tris.push_back({a, b, c});
            m.tris.push_back({a, c, d});
        }
    return m;
}
}  // namespace

TriMesh makeSphere(const Vec3& center, double radius, int segments, int rings) {
    TriMesh m;
    m.verts.push_back(center + Vec3(0, 0, -radius));
    for (int r = 1; r < rings; ++r) {
        const double phi = kPi * r / rings - kPi / 2;
        for (int s = 0; s < segments; ++s) {
            const double th = 2 * kPi * s / segments;
            m.verts.push_back(center + Vec3(std::cos(phi) * std::cos(th), std::cos(phi) * std::sin(th),
                                            std::sin(phi)) * radius);
        }
    }
    m.verts.push_back(center + Vec3(0, 0, radius));
    const int top = static_cast<int>(m.verts.size()) - 1;
    for (int s = 0; s < segments; ++s) m.tris.push_back({0, 1 + (s + 1) % segments, 1 + s});
    for (int r = 0; r < rings - 2; ++r)
        for (int s = 0; s < segments; ++s) {
            const int a = 1 + r * segments + s;
            const int b = 1 + r * segments + (s + 1) % segments;
            const int c = 1 + (r + 1) * segments + (s + 1) % segments;
            const int d = 1 + (r + 1) * segments + s;
            m.tris.push_back({a, b, c});
            m.tris.push_back({a, c, d});
        }
    const int last = 1 + (rings - 2) * segments;
    for (int s = 0; s < segments; ++s) m.tris.push_back({last + s, last + (s + 1) % segments, top});
    return m;
}

TriMesh makeCylinder(double radius, double z0, double z1, int segments, int rings) {
    return wrappedGrid(segments, rings, [&](double u, double v) {
        const double th = 2 * kPi * u;
        return Vec3(radius * std::cos(th), radius * std::sin(th), lerp(z0, z1, v));
    });
}

TriMesh makeTube(const Vec3& a, const Vec3& b, double radius, int segments, int rings) {
    const Vec3 axis = normalize(b - a);
    const Vec3 e1 = anyPerpendicular(axis);
    const Vec3 e2 = cross(axis, e1);
    return wrappedGrid(segments, rings, [&](double u, double v) {
        const double th = 2 * kPi * u;
        return lerp(a, b, v) + (e1 * std::cos(th) + e2 * std::sin(th)) * radius;
    });
}

TriMesh makeTorso(int segments, int rings) {
    // Height from z=-3 (hips) to z=3 (shoulders); rounded caps make it closed.
    TriMesh m = wrappedGrid(segments, rings, [&](double u, double v) {
        const double th = 2 * kPi * u;
        const double z = lerp(-3.4, 3.4, v);
        const double zc = clamp(z / 3.4, -1.0, 1.0);
        const double cap = std::sqrt(std::max(0.0, 1.0 - std::pow(std::fabs(zc), 6.0)));
        // Elliptic cross section (wider than deep), chest bulge, waist, pecs.
        const double waist = 1.0 - 0.12 * std::exp(-std::pow((z + 0.4) / 0.9, 2.0));
        const double chest = 1.0 + 0.10 * std::exp(-std::pow((z - 1.6) / 0.8, 2.0));
        const double pecs = 0.07 * std::exp(-std::pow((z - 1.5) / 0.6, 2.0)) *
                            std::pow(std::max(0.0, -std::sin(th)), 2.0) * std::cos(2 * th) * -1.0;
        const double spine = -0.05 * std::exp(-std::pow((th - kPi / 2) / 0.25, 2.0));
        const double rx = 1.9 * waist * chest * cap;
        const double ry = 1.25 * waist * chest * cap * (1.0 + pecs + spine);
        return Vec3(rx * std::cos(th), ry * std::sin(th), z);
    });
    return m;
}

TriMesh makeCharacter() {
    TriMesh m = makeTorso();
    m.append(makeSphere(Vec3(0, 0, 4.6), 1.05, 48, 24));
    m.append(makeTube(Vec3(1.6, 0, 2.4), Vec3(5.6, 0, 1.2), 0.55, 48, 32));
    m.append(makeTube(Vec3(-1.6, 0, 2.4), Vec3(-5.6, 0, 1.2), 0.55, 48, 32));
    return m;
}

Ray Camera::ray(double px, double py) const {
    const Vec3 f = forward();
    const Vec3 r = normalize(cross(f, up));
    const Vec3 u = cross(r, f);
    const double aspect = static_cast<double>(width) / height;
    const double th = std::tan(fovDeg * kPi / 360.0);
    const double nx = (2.0 * (px + 0.5) / width - 1.0) * th * aspect;
    const double ny = (1.0 - 2.0 * (py + 0.5) / height) * th;
    return Ray(eye, normalize(f + r * nx + u * ny));
}

bool Camera::project(const Vec3& p, double& sx, double& sy, double& depth) const {
    const Vec3 f = forward();
    const Vec3 r = normalize(cross(f, up));
    const Vec3 u = cross(r, f);
    const Vec3 d = p - eye;
    depth = dot(d, f);
    if (depth <= 1e-6) return false;
    const double aspect = static_cast<double>(width) / height;
    const double th = std::tan(fovDeg * kPi / 360.0);
    const double nx = dot(d, r) / depth / (th * aspect);
    const double ny = dot(d, u) / depth / th;
    sx = (nx + 1.0) * 0.5 * width - 0.5;
    sy = (1.0 - ny) * 0.5 * height - 0.5;
    return true;
}

std::vector<Ray> strokeRays(const Camera& cam, const std::vector<std::pair<double, double>>& pts,
                            double step) {
    std::vector<Ray> rays;
    if (pts.empty()) return rays;
    rays.push_back(cam.ray(pts[0].first, pts[0].second));
    for (size_t i = 1; i < pts.size(); ++i) {
        const double dx = pts[i].first - pts[i - 1].first;
        const double dy = pts[i].second - pts[i - 1].second;
        const int n = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy) / step)));
        for (int k = 1; k <= n; ++k) {
            const double t = static_cast<double>(k) / n;
            rays.push_back(cam.ray(pts[i - 1].first + dx * t, pts[i - 1].second + dy * t));
        }
    }
    return rays;
}

}  // namespace kit
}  // namespace rtp
