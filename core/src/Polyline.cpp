#include "rtp/Polyline.h"

#include "rtp/Surface.h"

#include <algorithm>

namespace rtp {

double Polyline::length() const {
    const auto c = cumulative();
    return c.empty() ? 0.0 : c.back();
}

std::vector<double> Polyline::cumulative() const {
    std::vector<double> c;
    if (pts.empty()) return c;
    c.reserve(pts.size() + 1);
    c.push_back(0.0);
    for (size_t i = 1; i < pts.size(); ++i) c.push_back(c.back() + distance(pts[i - 1], pts[i]));
    if (closed && pts.size() > 1) c.push_back(c.back() + distance(pts.back(), pts.front()));
    return c;
}

namespace {
// Locates arc length s: segment index i (from pts[i] to pts[i+1 mod n]) and local fraction.
void locate(const std::vector<double>& cum, double s, int& seg, double& frac) {
    const int segs = static_cast<int>(cum.size()) - 1;
    if (segs <= 0) { seg = 0; frac = 0; return; }
    if (s <= 0) { seg = 0; frac = 0; return; }
    if (s >= cum.back()) { seg = segs - 1; frac = 1; return; }
    const auto it = std::upper_bound(cum.begin(), cum.end(), s);
    seg = static_cast<int>(it - cum.begin()) - 1;
    seg = std::max(0, std::min(seg, segs - 1));
    const double len = cum[seg + 1] - cum[seg];
    frac = len > kEps ? (s - cum[seg]) / len : 0.0;
}

double wrap01(double t) {
    t = std::fmod(t, 1.0);
    return t < 0 ? t + 1.0 : t;
}
}  // namespace

Vec3 Polyline::pointAt(double t) const {
    if (pts.empty()) return Vec3();
    if (pts.size() == 1) return pts[0];
    const auto cum = cumulative();
    t = closed ? wrap01(t) : clamp(t, 0.0, 1.0);
    int seg;
    double frac;
    locate(cum, t * cum.back(), seg, frac);
    const Vec3& a = pts[seg];
    const Vec3& b = pts[(seg + 1) % pts.size()];
    return lerp(a, b, frac);
}

Vec3 Polyline::tangentAt(double t) const {
    if (pts.size() < 2) return Vec3(1, 0, 0);
    const auto cum = cumulative();
    t = closed ? wrap01(t) : clamp(t, 0.0, 1.0);
    int seg;
    double frac;
    locate(cum, t * cum.back(), seg, frac);
    const int n = size();
    // Blend neighbouring segment directions near vertices for a smoother frame.
    const Vec3 d = pts[(seg + 1) % n] - pts[seg];
    return normalize(d, Vec3(1, 0, 0));
}

std::vector<Vec3> Polyline::sampleAt(const std::vector<double>& params) const {
    std::vector<Vec3> out;
    out.reserve(params.size());
    if (pts.empty()) return out;
    const auto cum = cumulative();
    const int n = size();
    for (double t : params) {
        if (n == 1) { out.push_back(pts[0]); continue; }
        t = closed ? wrap01(t) : clamp(t, 0.0, 1.0);
        int seg;
        double frac;
        locate(cum, t * cum.back(), seg, frac);
        out.push_back(lerp(pts[seg], pts[(seg + 1) % n], frac));
    }
    return out;
}

Polyline Polyline::resampled(int count) const {
    Polyline out;
    out.closed = closed;
    if (pts.empty() || count < 1) return out;
    count = std::max(count, closed ? 3 : 2);
    std::vector<double> params(count);
    for (int i = 0; i < count; ++i)
        params[i] = closed ? static_cast<double>(i) / count : static_cast<double>(i) / (count - 1);
    out.pts = sampleAt(params);
    return out;
}

void Polyline::smooth(int iterations, double strength) {
    const int n = size();
    if (n < 3) return;
    std::vector<Vec3> tmp(pts);
    for (int it = 0; it < iterations; ++it) {
        for (int i = 0; i < n; ++i) {
            if (!closed && (i == 0 || i == n - 1)) { tmp[i] = pts[i]; continue; }
            const Vec3& a = pts[(i - 1 + n) % n];
            const Vec3& b = pts[(i + 1) % n];
            tmp[i] = lerp(pts[i], (a + b) * 0.5, strength);
        }
        pts.swap(tmp);
    }
}

void Polyline::removeDuplicates(double minDist) {
    if (pts.size() < 2) return;
    const double m2 = minDist * minDist;
    std::vector<Vec3> out;
    out.reserve(pts.size());
    out.push_back(pts[0]);
    for (size_t i = 1; i < pts.size(); ++i)
        if (distance2(pts[i], out.back()) > m2) out.push_back(pts[i]);
    if (!closed && out.size() >= 2 && distance2(pts.back(), out.back()) > 0) out.back() = pts.back();
    if (closed && out.size() > 2 && distance2(out.back(), out.front()) <= m2) out.pop_back();
    pts.swap(out);
}

void Polyline::reverse() { std::reverse(pts.begin(), pts.end()); }

void Polyline::rotateStart(double t) {
    if (!closed || pts.size() < 3) return;
    t = wrap01(t);
    if (t <= 1e-12) return;
    const auto cum = cumulative();
    int seg;
    double frac;
    locate(cum, t * cum.back(), seg, frac);
    const int n = size();
    const Vec3 start = lerp(pts[seg], pts[(seg + 1) % n], frac);
    std::vector<Vec3> out;
    out.reserve(n + 1);
    out.push_back(start);
    for (int k = 1; k <= n; ++k) {
        const Vec3& p = pts[(seg + k) % n];
        if (k == n && distance2(p, start) < 1e-24) break;
        if (k == 1 && distance2(p, start) < 1e-24) continue;
        out.push_back(p);
    }
    pts.swap(out);
}

double Polyline::closestParam(const Vec3& p, double* outDist) const {
    if (pts.empty()) { if (outDist) *outDist = kInf; return 0.0; }
    if (pts.size() == 1) { if (outDist) *outDist = distance(p, pts[0]); return 0.0; }
    const auto cum = cumulative();
    const int n = size();
    const int segs = segmentCount();
    double best2 = kInf, bestS = 0;
    for (int i = 0; i < segs; ++i) {
        const Vec3& a = pts[i];
        const Vec3& b = pts[(i + 1) % n];
        const Vec3 ab = b - a;
        const double l2 = length2(ab);
        const double f = l2 > kEps ? clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
        const double d2 = distance2(p, a + ab * f);
        if (d2 < best2) {
            best2 = d2;
            bestS = cum[i] + f * (cum[i + 1] - cum[i]);
        }
    }
    if (outDist) *outDist = std::sqrt(best2);
    return cum.back() > kEps ? bestS / cum.back() : 0.0;
}

Aabb Polyline::bounds() const {
    Aabb b;
    for (const auto& p : pts) b.expand(p);
    return b;
}

Vec3 Polyline::centroid() const {
    Vec3 c;
    if (pts.empty()) return c;
    for (const auto& p : pts) c += p;
    return c / static_cast<double>(pts.size());
}

// ---------------------------------------------------------------------------
double averageCurveDistance(const Polyline& a, const Polyline& b, int samples) {
    if (!a.valid() || !b.valid()) return 0.0;
    const Polyline ra = a.resampled(samples);
    const Polyline rb = b.resampled(samples);
    double sum = 0;
    for (int i = 0; i < ra.size(); ++i) sum += distance(ra.pts[i], rb.pts[i]);
    return sum / ra.size();
}

namespace {
// Closest points between segments p1q1 and p2q2 (Ericson 5.1.9).
double segmentSegment(const Vec3& p1, const Vec3& q1, const Vec3& p2, const Vec3& q2, double& s,
                      double& t) {
    const Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const double a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
    if (a <= kEps && e <= kEps) { s = t = 0; return distance(p1, p2); }
    if (a <= kEps) {
        s = 0;
        t = clamp(f / e, 0.0, 1.0);
    } else {
        const double c = dot(d1, r);
        if (e <= kEps) {
            t = 0;
            s = clamp(-c / a, 0.0, 1.0);
        } else {
            const double b = dot(d1, d2);
            const double denom = a * e - b * b;
            s = denom > kEps ? clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = clamp(-c / a, 0.0, 1.0); }
            else if (t > 1) { t = 1; s = clamp((b - c) / a, 0.0, 1.0); }
        }
    }
    return distance(p1 + d1 * s, p2 + d2 * t);
}
}  // namespace

double closestApproach(const Polyline& a, const Polyline& b, double& paramA, double& paramB) {
    paramA = paramB = 0;
    if (!a.valid() || !b.valid()) return kInf;
    const auto ca = a.cumulative();
    const auto cb = b.cumulative();
    const int na = a.size(), nb = b.size();
    double best = kInf;
    for (int i = 0; i < a.segmentCount(); ++i) {
        const Vec3& a0 = a.pts[i];
        const Vec3& a1 = a.pts[(i + 1) % na];
        for (int j = 0; j < b.segmentCount(); ++j) {
            double s, t;
            const double d = segmentSegment(a0, a1, b.pts[j], b.pts[(j + 1) % nb], s, t);
            if (d < best) {
                best = d;
                paramA = (ca[i] + s * (ca[i + 1] - ca[i])) / std::max(ca.back(), kEps);
                paramB = (cb[j] + t * (cb[j + 1] - cb[j])) / std::max(cb.back(), kEps);
            }
        }
    }
    return best;
}

bool alignDirection(const Polyline& a, Polyline& b) {
    if (!a.valid() || !b.valid()) return false;
    const Vec3 a0 = a.pts.front(), a1 = a.pts.back();
    const Vec3 b0 = b.pts.front(), b1 = b.pts.back();
    const double same = distance(a0, b0) + distance(a1, b1);
    const double crossed = distance(a0, b1) + distance(a1, b0);
    if (crossed < same) {
        b.reverse();
        return true;
    }
    return false;
}

void bestClosedAlignment(const Polyline& a, const Polyline& b, bool& reverse, double& shift,
                         int samples) {
    reverse = false;
    shift = 0.0;
    if (!a.valid() || !b.valid()) return;
    const Polyline ra = a.resampled(samples);
    double bestCost = kInf;
    for (int dir = 0; dir < 2; ++dir) {
        Polyline cand = b;
        if (dir == 1) cand.reverse();
        const Polyline rb = cand.resampled(samples);
        std::vector<double> cost(samples, 0.0);
        for (int k = 0; k < samples; ++k) {
            double c = 0;
            for (int i = 0; i < samples; ++i) c += distance2(ra.pts[i], rb.pts[(i + k) % samples]);
            cost[k] = c;
        }
        for (int k = 0; k < samples; ++k) {
            if (cost[k] < bestCost) {
                bestCost = cost[k];
                // Parabolic refinement between neighbouring integer shifts.
                const double cm = cost[(k - 1 + samples) % samples];
                const double cp = cost[(k + 1) % samples];
                const double den = cm - 2 * cost[k] + cp;
                const double frac = den > kEps ? clamp(0.5 * (cm - cp) / den, -0.5, 0.5) : 0.0;
                shift = (k + frac) / samples;
                reverse = dir == 1;
            }
        }
    }
}

double alignClosed(const Polyline& a, Polyline& b, int samples) {
    bool reverse;
    double shift;
    bestClosedAlignment(a, b, reverse, shift, samples);
    if (reverse) b.reverse();
    b.rotateStart(shift);
    return shift;
}

Vec3 catmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t) {
    // Centripetal parameterisation (alpha = 0.5) avoids cusps and self intersections.
    auto knot = [](const Vec3& a, const Vec3& b) {
        return std::max(std::sqrt(distance(a, b)), 1e-6);
    };
    const double t0 = 0;
    const double t1 = t0 + knot(p0, p1);
    const double t2 = t1 + knot(p1, p2);
    const double t3 = t2 + knot(p2, p3);
    const double tt = lerp(t1, t2, t);
    const Vec3 a1 = p0 * ((t1 - tt) / (t1 - t0)) + p1 * ((tt - t0) / (t1 - t0));
    const Vec3 a2 = p1 * ((t2 - tt) / (t2 - t1)) + p2 * ((tt - t1) / (t2 - t1));
    const Vec3 a3 = p2 * ((t3 - tt) / (t3 - t2)) + p3 * ((tt - t2) / (t3 - t2));
    const Vec3 b1 = a1 * ((t2 - tt) / (t2 - t0)) + a2 * ((tt - t0) / (t2 - t0));
    const Vec3 b2 = a2 * ((t3 - tt) / (t3 - t1)) + a3 * ((tt - t1) / (t3 - t1));
    return b1 * ((t2 - tt) / (t2 - t1)) + b2 * ((tt - t1) / (t2 - t1));
}

void conformPolyline(Polyline& line, const Surface& surface, double offset) {
    for (auto& p : line.pts) {
        Vec3 q;
        if (surface.project(p, offset, q)) p = q;
    }
}

}  // namespace rtp
