// TopoWeave core - turning viewport strokes into surface curves.
//
// The host (3ds Max) converts every mouse sample into a ray in the
// retopology object's space. Everything else happens here, so the exact same
// code path runs in the plug-in and in the unit tests.
#pragma once

#include "rtp/Polyline.h"

#include <vector>

namespace rtp {

class Surface;

struct StrokeOptions {
    double offset = 0.0;          // lift above the surface
    int smoothIterations = 3;     // on-surface smoothing passes
    double sampleSpacing = 0.0;   // 0 = automatic (1/100 of stroke length, clamped)
    double closeTolerance = 0.08; // endpoints closer than this fraction of length => closed
    bool allowClose = true;
    bool forceClosed = false;     // host can force closure (e.g. screen space test)
};

struct StrokeResult {
    bool ok = false;
    Polyline curve;
    int hits = 0;   // rays that hit the surface
    int misses = 0;
    const char* message = "";
};

// Surface stroke: each ray is cast against the surface; misses are dropped,
// depth discontinuities (stroke slipping behind a silhouette) split the stroke
// and the longest continuous piece is kept. The result is resampled, smoothed
// on the surface and lifted by offset.
StrokeResult strokeFromRays(const std::vector<Ray>& rays, const Surface& surface,
                            const StrokeOptions& opt);

struct ContourOptions {
    double offset = 0.0;
    int smoothIterations = 1;
    // Viewport forward direction (object space). When given, the cutting plane
    // contains this direction instead of the diverging perspective rays, so
    // rings come out square to the limb rather than fanning from the camera.
    Vec3 viewDir;
};

// Contour stroke: the user drags *across* a limb. The rays of the stroke span
// a plane (the stroke line swept along the view direction); the surface is
// cut with that plane and the cut loop passing under the stroke becomes a
// ring that wraps all the way around the limb, including the hidden back side.
StrokeResult contourFromRays(const std::vector<Ray>& rays, const Surface& surface,
                             const ContourOptions& opt);

// Like contourFromRays but with an explicit plane (used by tests and by the
// "re-slice" adjust gesture).
StrokeResult contourFromPlane(const Plane& plane, const Vec3& nearPoint, const Surface& surface,
                              const ContourOptions& opt);

}  // namespace rtp
