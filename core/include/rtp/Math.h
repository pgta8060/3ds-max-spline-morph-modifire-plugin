// TopoWeave core - small, dependency-free vector math.
//
// The core library is deliberately independent from the 3ds Max SDK so that
// every geometric algorithm can be compiled and unit-tested on any platform.
// Double precision is used internally; the Max adapter converts to/from Point3.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace rtp {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-12;
constexpr double kInf = std::numeric_limits<double>::infinity();

struct Vec3 {
    double x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    double& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(double s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(double s) { x /= s; y /= s; z /= s; return *this; }
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

inline double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double length2(const Vec3& v) { return dot(v, v); }
inline double length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline double distance(const Vec3& a, const Vec3& b) { return length(a - b); }
inline double distance2(const Vec3& a, const Vec3& b) { return length2(a - b); }

inline Vec3 normalize(const Vec3& v, const Vec3& fallback = Vec3(0, 0, 1)) {
    const double l = length(v);
    return l > kEps ? v / l : fallback;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, double t) { return a + (b - a) * t; }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double clamp(double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); }
inline double smoothstep(double t) {
    t = clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline Vec3 vmin(const Vec3& a, const Vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
inline Vec3 vmax(const Vec3& a, const Vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

// Removes the component of v along unit normal n.
inline Vec3 projectOnPlane(const Vec3& v, const Vec3& n) { return v - n * dot(v, n); }

// Any unit vector perpendicular to n.
inline Vec3 anyPerpendicular(const Vec3& n) {
    const Vec3 a = std::fabs(n.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    return normalize(cross(n, a));
}

// Rotates v around unit axis by angle (radians) - Rodrigues' formula.
inline Vec3 rotateAround(const Vec3& v, const Vec3& axis, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0 - c));
}

struct Ray {
    Vec3 origin;
    Vec3 dir;  // expected to be normalized
    Ray() = default;
    Ray(const Vec3& o, const Vec3& d) : origin(o), dir(d) {}
    Vec3 at(double t) const { return origin + dir * t; }
};

struct Plane {
    Vec3 n;      // unit normal
    double d = 0; // plane: dot(n, p) == d
    Plane() = default;
    Plane(const Vec3& normal, const Vec3& point) : n(normalize(normal)), d(dot(n, point)) {}
    double signedDistance(const Vec3& p) const { return dot(n, p) - d; }
};

struct Aabb {
    Vec3 lo{kInf, kInf, kInf};
    Vec3 hi{-kInf, -kInf, -kInf};

    void expand(const Vec3& p) { lo = vmin(lo, p); hi = vmax(hi, p); }
    void expand(const Aabb& b) { lo = vmin(lo, b.lo); hi = vmax(hi, b.hi); }
    bool valid() const { return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z; }
    Vec3 center() const { return (lo + hi) * 0.5; }
    Vec3 extent() const { return hi - lo; }
    double diagonal() const { return valid() ? length(hi - lo) : 0.0; }
    int longestAxis() const {
        const Vec3 e = extent();
        return (e.x >= e.y && e.x >= e.z) ? 0 : (e.y >= e.z ? 1 : 2);
    }
    // Squared distance from p to the box (0 if inside).
    double distance2(const Vec3& p) const {
        double d2 = 0;
        for (int i = 0; i < 3; ++i) {
            const double v = p[i];
            if (v < lo[i]) d2 += (lo[i] - v) * (lo[i] - v);
            else if (v > hi[i]) d2 += (v - hi[i]) * (v - hi[i]);
        }
        return d2;
    }
    // Slab test. Returns entry distance in tNear (may be negative if origin inside).
    bool intersect(const Ray& r, const Vec3& invDir, double tMax, double& tNear) const {
        double t0 = 0.0, t1 = tMax;
        for (int i = 0; i < 3; ++i) {
            double tA = (lo[i] - r.origin[i]) * invDir[i];
            double tB = (hi[i] - r.origin[i]) * invDir[i];
            if (tA > tB) std::swap(tA, tB);
            t0 = tA > t0 ? tA : t0;
            t1 = tB < t1 ? tB : t1;
            if (t0 > t1) return false;
        }
        tNear = t0;
        return true;
    }
};

}  // namespace rtp
