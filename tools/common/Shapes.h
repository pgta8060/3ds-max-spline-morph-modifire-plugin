// Procedural reference surfaces and a virtual camera used by the tests and the
// offline demo. The camera turns 2D "mouse" strokes into rays exactly like the
// 3ds Max viewport does, so tests exercise the real stroke pipeline.
#pragma once

#include "rtp/Math.h"
#include "rtp/Surface.h"

#include <vector>

namespace rtp {
namespace kit {

TriMesh makeSphere(const Vec3& center, double radius, int segments = 48, int rings = 24);
// Open cylinder along +Z from z0 to z1 (no caps).
TriMesh makeCylinder(double radius, double z0, double z1, int segments = 64, int rings = 32);
// Cylinder along an arbitrary axis.
TriMesh makeTube(const Vec3& a, const Vec3& b, double radius, int segments = 48, int rings = 24);
// Organic "torso": a closed, bumpy capsule along Z. Radius varies with angle
// and height (chest, waist, back muscles) to mimic a character body.
TriMesh makeTorso(int segments = 96, int rings = 72);
// Torso + head + left/right arm tubes, merged into one High Poly.
TriMesh makeCharacter();

struct Camera {
    Vec3 eye{0, -10, 0};
    Vec3 target{0, 0, 0};
    Vec3 up{0, 0, 1};
    double fovDeg = 40;
    int width = 960;
    int height = 720;

    Ray ray(double px, double py) const;
    // Screen position (pixels) and view depth; false if behind the camera.
    bool project(const Vec3& p, double& sx, double& sy, double& depth) const;
    Vec3 forward() const { return normalize(target - eye); }
};

// Rays along a 2D polyline in screen space, sampled every `step` pixels.
std::vector<Ray> strokeRays(const Camera& cam, const std::vector<std::pair<double, double>>& pts,
                            double step = 3.0);

}  // namespace kit
}  // namespace rtp
