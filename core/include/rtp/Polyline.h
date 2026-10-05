// TopoWeave core - polyline utilities (the "splines" users draw).
#pragma once

#include "rtp/Math.h"

#include <vector>

namespace rtp {

class Surface;

struct Polyline {
    std::vector<Vec3> pts;
    bool closed = false;

    Polyline() = default;
    Polyline(std::vector<Vec3> p, bool c) : pts(std::move(p)), closed(c) {}

    int size() const { return static_cast<int>(pts.size()); }
    bool valid() const { return pts.size() >= 2; }
    int segmentCount() const { return closed ? size() : size() - 1; }

    double length() const;
    // Cumulative arc length per vertex (size()+1 entries when closed: last = total).
    std::vector<double> cumulative() const;

    // Point at normalized arc length t in [0,1] (wraps when closed).
    Vec3 pointAt(double t) const;
    // Unit tangent at normalized arc length t.
    Vec3 tangentAt(double t) const;

    // Uniform arc length resampling. For open curves both end points are kept
    // and the result has `count` points; closed curves get `count` points with
    // no duplicated seam point.
    Polyline resampled(int count) const;
    // Resamples at the given normalized parameters.
    std::vector<Vec3> sampleAt(const std::vector<double>& params) const;

    // Laplacian smoothing; open curve end points stay fixed.
    void smooth(int iterations, double strength = 0.5);
    // Removes consecutive points closer than minDist.
    void removeDuplicates(double minDist);
    void reverse();
    // Rotates a closed curve so that it starts at normalized parameter t.
    void rotateStart(double t);

    // Closest point on the curve: returns normalized parameter and distance.
    double closestParam(const Vec3& p, double* outDist = nullptr) const;

    Aabb bounds() const;
    Vec3 centroid() const;
};

// Average distance between two curves sampled at matching normalized params.
double averageCurveDistance(const Polyline& a, const Polyline& b, int samples = 32);

// Closest approach between two polylines. Returns distance; params are normalized.
double closestApproach(const Polyline& a, const Polyline& b, double& paramA, double& paramB);

// Makes b run in the same direction as a (open curves). Returns true if reversed.
bool alignDirection(const Polyline& a, Polyline& b);

// For closed curves: finds whether b must be reversed and by which normalized
// parameter its start must be shifted (after reversal) to best match a.
void bestClosedAlignment(const Polyline& a, const Polyline& b, bool& reverse, double& shift,
                         int samples = 64);

// For closed curves: reverses and/or rotates b so that its start and winding
// match a as closely as possible. Returns the applied start parameter shift.
double alignClosed(const Polyline& a, Polyline& b, int samples = 64);

// Centripetal Catmull-Rom through p0..p3 evaluated on the p1-p2 span, t in [0,1].
Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t);

// Projects every point onto the surface (keeps points that fail unchanged).
void conformPolyline(Polyline& line, const Surface& surface, double offset);

}  // namespace rtp
