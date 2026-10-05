#include "rtp/Stroke.h"

#include "rtp/Surface.h"

#include <algorithm>

namespace rtp {

namespace {

void smoothOnSurface(Polyline& curve, const Surface& surface, int iterations) {
    for (int i = 0; i < iterations; ++i) {
        curve.smooth(1, 0.5);
        conformPolyline(curve, surface, 0.0);
    }
}

}  // namespace

StrokeResult strokeFromRays(const std::vector<Ray>& rays, const Surface& surface,
                            const StrokeOptions& opt) {
    StrokeResult res;
    if (surface.empty()) { res.message = "No High Poly source assigned"; return res; }
    if (rays.size() < 2) { res.message = "Stroke too short"; return res; }

    struct Sample { Vec3 p; double t; int ray; };
    std::vector<std::vector<Sample>> pieces(1);
    const double scale = std::max(surface.scale(), 1e-9);
    for (int i = 0; i < static_cast<int>(rays.size()); ++i) {
        SurfaceHit hit;
        if (!surface.raycast(rays[i], hit)) {
            ++res.misses;
            if (!pieces.back().empty()) pieces.emplace_back();
            continue;
        }
        ++res.hits;
        auto& cur = pieces.back();
        if (!cur.empty()) {
            // Detect depth discontinuities: compare the 3D step with the
            // lateral step the two rays make at the previous depth.
            const Sample& prev = cur.back();
            const double step = distance(prev.p, hit.point);
            const double lateral = distance(prev.p, rays[i].at(prev.t));
            const double depthJump = std::fabs(hit.t - prev.t);
            if (step > 6.0 * lateral + scale * 1e-3 && depthJump > scale * 0.02) pieces.emplace_back();
        }
        pieces.back().push_back({hit.point, hit.t, i});
    }

    // Keep the longest continuous piece.
    const std::vector<Sample>* best = nullptr;
    double bestLen = -1;
    for (const auto& piece : pieces) {
        double len = 0;
        for (size_t k = 1; k < piece.size(); ++k) len += distance(piece[k - 1].p, piece[k].p);
        if (piece.size() >= 2 && len > bestLen) { bestLen = len; best = &piece; }
    }
    if (!best) { res.message = "Stroke does not touch the High Poly"; return res; }

    Polyline curve;
    for (const auto& s : *best) curve.pts.push_back(s.p);
    curve.removeDuplicates(scale * 1e-6);
    if (curve.size() < 2) { res.message = "Stroke too short"; return res; }

    const double len = curve.length();
    if (len <= scale * 1e-5) { res.message = "Stroke too short"; return res; }
    const double endGap = distance(curve.pts.front(), curve.pts.back());
    const bool closed = opt.forceClosed ||
                        (opt.allowClose && curve.size() >= 8 && endGap < opt.closeTolerance * len);
    if (closed) {
        curve.closed = true;
        curve.removeDuplicates(std::max(endGap * 1.01, scale * 1e-6));
    }

    const double spacing = opt.sampleSpacing > 0 ? opt.sampleSpacing : len / 100.0;
    const int count = static_cast<int>(clamp(len / std::max(spacing, 1e-12), 8.0, 400.0));
    curve = curve.resampled(count);
    smoothOnSurface(curve, surface, opt.smoothIterations);
    if (opt.offset != 0.0) conformPolyline(curve, surface, opt.offset);

    res.curve = std::move(curve);
    res.ok = true;
    return res;
}

StrokeResult contourFromPlane(const Plane& plane, const Vec3& nearPoint, const Surface& surface,
                              const ContourOptions& opt) {
    StrokeResult res;
    const auto loops = surface.slice(plane);
    if (loops.empty()) { res.message = "Contour plane does not cut the High Poly"; return res; }

    int bestIdx = -1;
    double bestD = kInf;
    for (int i = 0; i < static_cast<int>(loops.size()); ++i) {
        Polyline pl(loops[i].points, loops[i].closed);
        if (!pl.valid()) continue;
        double d;
        pl.closestParam(nearPoint, &d);
        if (d < bestD) { bestD = d; bestIdx = i; }
    }
    if (bestIdx < 0) { res.message = "Contour plane does not cut the High Poly"; return res; }

    Polyline ring(loops[bestIdx].points, loops[bestIdx].closed);
    ring.removeDuplicates(surface.scale() * 1e-6);
    if (!ring.valid()) { res.message = "Contour is degenerate"; return res; }

    if (ring.closed) {
        // Consistent winding around the plane normal so consecutive rings match.
        const Vec3 c = ring.centroid();
        Vec3 area;
        for (int i = 0; i < ring.size(); ++i)
            area += cross(ring.pts[i] - c, ring.pts[(i + 1) % ring.size()] - c);
        if (dot(area, plane.n) < 0) ring.reverse();
        ring.rotateStart(ring.closestParam(nearPoint));
    } else if (ring.closestParam(nearPoint) > 0.5) {
        ring.reverse();
    }

    const int count = std::max(16, std::min(256, ring.size()));
    ring = ring.resampled(count);
    for (int i = 0; i < opt.smoothIterations; ++i) {
        ring.smooth(1, 0.5);
        conformPolyline(ring, surface, 0.0);
    }
    if (opt.offset != 0.0) conformPolyline(ring, surface, opt.offset);

    res.curve = std::move(ring);
    res.ok = true;
    return res;
}

StrokeResult contourFromRays(const std::vector<Ray>& rays, const Surface& surface,
                             const ContourOptions& opt) {
    StrokeResult res;
    if (surface.empty()) { res.message = "No High Poly source assigned"; return res; }
    if (rays.size() < 2) { res.message = "Stroke too short"; return res; }

    // Anchor: the hit closest to the middle of the stroke.
    const int n = static_cast<int>(rays.size());
    int anchor = -1;
    SurfaceHit anchorHit;
    for (int k = 0; k < n; ++k) {
        SurfaceHit h;
        if (surface.raycast(rays[k], h)) {
            ++res.hits;
            if (anchor < 0 || std::abs(k - n / 2) < std::abs(anchor - n / 2)) {
                anchor = k;
                anchorHit = h;
            }
        } else {
            ++res.misses;
        }
    }
    if (anchor < 0) { res.message = "Contour stroke must cross the High Poly"; return res; }

    const double depth = anchorHit.t;
    const Vec3 a = rays.front().at(depth);
    const Vec3 b = rays.back().at(depth);
    const Vec3 strokeDir = b - a;
    if (length(strokeDir) < surface.scale() * 1e-4) { res.message = "Stroke too short"; return res; }
    const Vec3 view = length2(opt.viewDir) > kEps ? normalize(opt.viewDir) : normalize(rays[anchor].dir);
    const Vec3 normal = cross(strokeDir, view);
    if (length(normal) < kEps) { res.message = "Stroke is parallel to the view"; return res; }

    const Plane plane(normal, anchorHit.point);
    StrokeResult ring = contourFromPlane(plane, anchorHit.point, surface, opt);
    ring.hits = res.hits;
    ring.misses = res.misses;
    return ring;
}

}  // namespace rtp
