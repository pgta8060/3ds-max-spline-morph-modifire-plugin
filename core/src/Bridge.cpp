#include "rtp/Bridge.h"

#include "rtp/Surface.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rtp {

namespace {

// A guide after direction / start alignment against its predecessor.
struct AlignedGuide {
    Polyline curve;
    std::vector<int> border;           // aligned vertex ids (border guides only)
    std::vector<double> borderParams;  // normalized arc length of every border vertex
    bool isBorder() const { return !border.empty(); }
};

std::vector<double> vertexParams(const Polyline& pl) {
    const auto cum = pl.cumulative();
    std::vector<double> p(pl.pts.size(), 0.0);
    const double total = cum.empty() ? 0.0 : cum.back();
    for (size_t i = 0; i < pl.pts.size(); ++i) p[i] = total > kEps ? cum[i] / total : 0.0;
    return p;
}

void rotateBorder(AlignedGuide& g, int k) {
    const int n = static_cast<int>(g.border.size());
    if (n == 0) return;
    k = ((k % n) + n) % n;
    if (k == 0) return;
    std::rotate(g.border.begin(), g.border.begin() + k, g.border.end());
    std::rotate(g.curve.pts.begin(), g.curve.pts.begin() + k, g.curve.pts.end());
}

// Plain reversal (same convention as bestClosedAlignment, so the computed
// start shift applies unchanged afterwards).
void reverseGuide(AlignedGuide& g) {
    g.curve.reverse();
    std::reverse(g.border.begin(), g.border.end());
}

AlignedGuide alignGuide(const Guide& guide, const AlignedGuide* prev) {
    AlignedGuide g;
    g.curve = guide.curve;
    g.border = guide.borderVerts;
    if (prev) {
        if (!g.curve.closed) {
            Polyline tmp = g.curve;
            if (alignDirection(prev->curve, tmp)) reverseGuide(g);
        } else {
            bool reverse;
            double shift;
            bestClosedAlignment(prev->curve, g.curve, reverse, shift);
            if (reverse) reverseGuide(g);
            if (g.isBorder()) {
                // Borders can only rotate by whole vertices.
                const auto params = vertexParams(g.curve);
                int bestK = 0;
                double bestD = kInf;
                for (size_t k = 0; k < params.size(); ++k) {
                    double d = std::fabs(params[k] - shift);
                    d = std::min(d, 1.0 - d);
                    if (d < bestD) { bestD = d; bestK = static_cast<int>(k); }
                }
                rotateBorder(g, bestK);
            } else {
                g.curve.rotateStart(shift);
            }
        }
    }
    if (g.curve.closed && guide.twist != 0.0) {
        if (g.isBorder()) rotateBorder(g, static_cast<int>(std::lround(guide.twist * g.border.size())));
        else g.curve.rotateStart(guide.twist);
    }
    if (g.isBorder()) g.borderParams = vertexParams(g.curve);
    return g;
}

double wrap01(double t) {
    t = std::fmod(t, 1.0);
    return t < 0 ? t + 1.0 : t;
}

// Base column shape across the guides: centripetal Catmull-Rom with
// reflected phantom points at the ends (linear when only two guides).
Vec3 columnBase(const std::vector<Vec3>& q, int span, double t, bool smooth) {
    const int m = static_cast<int>(q.size());
    const Vec3& q1 = q[span];
    const Vec3& q2 = q[span + 1];
    if (!smooth || m == 2) return lerp(q1, q2, t);
    const Vec3 q0 = span > 0 ? q[span - 1] : q1 * 2.0 - q2;
    const Vec3 q3 = span + 2 < m ? q[span + 2] : q2 * 2.0 - q1;
    return catmullRom(q0, q1, q2, q3, t);
}

struct Frame {
    Vec3 e1, e2, e3;
    double len = 1.0;
};

Frame spanFrame(const Vec3& a, const Vec3& b, const Vec3& guideTangent) {
    Frame f;
    const Vec3 d = b - a;
    f.len = std::max(length(d), 1e-12);
    f.e1 = normalize(d, Vec3(1, 0, 0));
    f.e2 = normalize(projectOnPlane(guideTangent, f.e1), anyPerpendicular(f.e1));
    f.e3 = cross(f.e1, f.e2);
    return f;
}

// Rail influence for one span: deviation coefficients (in span frame units)
// for every row parameter of the span, located at column parameter x.
struct RailSpan {
    double x = 0;
    std::vector<Vec3> coeff;  // V+1 entries
};

}  // namespace

// ---------------------------------------------------------------------------
// Stroke management
// ---------------------------------------------------------------------------
int BridgeSession::addGuide(const Polyline& curve) {
    Guide g;
    g.curve = curve;
    g.id = state_.nextId++;
    auto& gs = state_.guides;
    if (gs.size() < 2) {
        // With one guide, keep drawing order unless the new guide is a border
        // continuation placed first; nothing to decide yet.
        gs.push_back(g);
        return static_cast<int>(gs.size()) - 1;
    }
    // Insert between two consecutive guides when the new one lies between them.
    const Vec3 c = curve.centroid();
    int bestPair = -1;
    double bestExcess = kInf;
    for (size_t i = 0; i + 1 < gs.size(); ++i) {
        const Vec3 a = gs[i].curve.centroid(), b = gs[i + 1].curve.centroid();
        const double ab = distance(a, b);
        const double excess = (distance(c, a) + distance(c, b) - ab) / std::max(ab, 1e-12);
        if (excess < bestExcess) { bestExcess = excess; bestPair = static_cast<int>(i); }
    }
    if (bestPair >= 0 && bestExcess < 0.35) {
        gs.insert(gs.begin() + bestPair + 1, g);
        return bestPair + 1;
    }
    const double dFirst = distance(c, gs.front().curve.centroid());
    const double dLast = distance(c, gs.back().curve.centroid());
    if (dFirst < dLast && !gs.front().isBorder()) {
        gs.insert(gs.begin(), g);
        return 0;
    }
    gs.push_back(g);
    return static_cast<int>(gs.size()) - 1;
}

int BridgeSession::addBorderGuide(const PolyMesh& mesh, const BorderChain& chain) {
    Guide g;
    g.curve = chainToPolyline(mesh, chain);
    g.borderVerts = chain.verts;
    g.id = state_.nextId++;
    auto& gs = state_.guides;
    if (gs.empty()) {
        gs.push_back(g);
        return 0;
    }
    // A border picked after free guides closes the band at the nearer end.
    const Vec3 c = g.curve.centroid();
    if (distance(c, gs.front().curve.centroid()) < distance(c, gs.back().curve.centroid()) &&
        gs.size() > 1) {
        gs.insert(gs.begin(), g);
        return 0;
    }
    gs.push_back(g);
    return static_cast<int>(gs.size()) - 1;
}

int BridgeSession::addRail(const Polyline& curve) {
    Rail r;
    r.curve = curve;
    r.id = state_.nextId++;
    state_.rails.push_back(r);
    return static_cast<int>(state_.rails.size()) - 1;
}

double BridgeSession::railTolerance() const {
    if (settings_.railTolerance > 0) return settings_.railTolerance;
    const double floorTol = surface_ ? surface_->scale() * 0.004 : 1e-3;
    const auto& gs = state_.guides;
    if (gs.size() < 2) return surface_ ? surface_->scale() * 0.02 : 1.0;
    double minGap = kInf;
    for (size_t i = 0; i + 1 < gs.size(); ++i)
        minGap = std::min(minGap, averageCurveDistance(gs[i].curve, gs[i + 1].curve, 16));
    return std::max(floorTol, 0.3 * minGap);
}

StrokeRole BridgeSession::classify(const Polyline& curve) const {
    if (state_.guides.size() < 2 || !curve.valid()) return StrokeRole::Guide;
    const double tol = railTolerance();
    int crossed = 0;
    for (const auto& g : state_.guides) {
        double a, b;
        if (closestApproach(curve, g.curve, a, b) <= tol) ++crossed;
    }
    return crossed >= 2 ? StrokeRole::Rail : StrokeRole::Guide;
}

StrokeRole BridgeSession::addAuto(const Polyline& curve) {
    const StrokeRole role = classify(curve);
    if (role == StrokeRole::Rail) addRail(curve);
    else addGuide(curve);
    return role;
}

bool BridgeSession::nearestStroke(const Vec3& p, double tolerance, StrokeRole& role,
                                  int& index) const {
    double best = tolerance;
    bool found = false;
    for (size_t i = 0; i < state_.guides.size(); ++i) {
        double d;
        state_.guides[i].curve.closestParam(p, &d);
        if (d <= best) { best = d; role = StrokeRole::Guide; index = static_cast<int>(i); found = true; }
    }
    for (size_t i = 0; i < state_.rails.size(); ++i) {
        double d;
        state_.rails[i].curve.closestParam(p, &d);
        if (d <= best) { best = d; role = StrokeRole::Rail; index = static_cast<int>(i); found = true; }
    }
    return found;
}

void BridgeSession::removeStroke(StrokeRole role, int index) {
    if (role == StrokeRole::Guide) {
        if (index >= 0 && index < static_cast<int>(state_.guides.size()))
            state_.guides.erase(state_.guides.begin() + index);
    } else if (index >= 0 && index < static_cast<int>(state_.rails.size())) {
        state_.rails.erase(state_.rails.begin() + index);
    }
}

void BridgeSession::removeLast() {
    int gi = -1, ri = -1, gid = 0, rid = 0;
    for (size_t i = 0; i < state_.guides.size(); ++i)
        if (state_.guides[i].id > gid) { gid = state_.guides[i].id; gi = static_cast<int>(i); }
    for (size_t i = 0; i < state_.rails.size(); ++i)
        if (state_.rails[i].id > rid) { rid = state_.rails[i].id; ri = static_cast<int>(i); }
    if (gi < 0 && ri < 0) return;
    if (rid > gid) removeStroke(StrokeRole::Rail, ri);
    else removeStroke(StrokeRole::Guide, gi);
}

void BridgeSession::clear() {
    state_.guides.clear();
    state_.rails.clear();
}

void BridgeSession::setTwist(int guideIndex, double twist) {
    if (guideIndex >= 0 && guideIndex < static_cast<int>(state_.guides.size()))
        state_.guides[guideIndex].twist = twist;
}

// ---------------------------------------------------------------------------
// Band construction
// ---------------------------------------------------------------------------
BridgePreview BridgeSession::build() const {
    BridgePreview out;
    const auto& gs = state_.guides;
    if (!surface_ || surface_->empty()) { out.message = "Assign a High Poly source first"; return out; }
    if (gs.size() < 2) {
        out.message = gs.empty() ? "Draw a guide spline on the High Poly"
                                 : "Draw a second guide to create the band";
        if (!gs.empty()) out.alignedGuides.push_back(gs[0].curve);
        return out;
    }
    const bool closed = gs[0].curve.closed;
    for (const auto& g : gs) {
        if (!g.curve.valid()) { out.message = "A guide is degenerate"; return out; }
        if (g.curve.closed != closed) {
            out.message = "Cannot bridge a closed loop with an open spline";
            return out;
        }
    }

    // 1. Align every guide to its predecessor (direction, loop start, twist).
    std::vector<AlignedGuide> G;
    G.reserve(gs.size());
    for (size_t i = 0; i < gs.size(); ++i) G.push_back(alignGuide(gs[i], i ? &G[i - 1] : nullptr));
    for (const auto& g : G) out.alignedGuides.push_back(g.curve);
    const int m = static_cast<int>(G.size());

    // 2. Column count and base distribution (borders dictate both).
    int U = std::max(settings_.uSegments, closed ? 3 : 1);
    int firstBorder = -1;
    for (int i = 0; i < m; ++i) {
        if (!G[i].isBorder()) continue;
        const int edges = closed ? static_cast<int>(G[i].border.size())
                                 : static_cast<int>(G[i].border.size()) - 1;
        if (firstBorder < 0) {
            firstBorder = i;
            U = edges;
        } else if (edges != U) {
            std::ostringstream s;
            s << "Borders have different edge counts (" << U << " vs " << edges << ")";
            out.message = s.str();
            return out;
        }
    }
    if (U < (closed ? 3 : 1)) { out.message = "Border is too short"; return out; }
    const int cols = closed ? U : U + 1;

    MonotoneMap base;
    if (firstBorder >= 0) {
        std::vector<MonotoneMap::Anchor> anchors;
        const auto& p = G[firstBorder].borderParams;
        for (int k = 0; k < U; ++k)
            if (closed || k > 0) anchors.push_back({static_cast<double>(k) / U, p[k]});
        base.setAnchors(anchors, closed);
    } else {
        base.setIdentity(closed);
    }
    std::vector<double> xk(cols);
    for (int k = 0; k < cols; ++k) xk[k] = base(static_cast<double>(k) / U);

    // 3. Rails: crossings with each guide.
    struct Crossing { bool valid = false; double u = 0, s = 0; };
    const double tol = railTolerance();
    const int nr = static_cast<int>(state_.rails.size());
    std::vector<std::vector<Crossing>> crossings(nr, std::vector<Crossing>(m));
    std::vector<double> railX(nr, 0.0);
    std::vector<char> railUsed(nr, 0);
    for (int j = 0; j < nr; ++j) {
        const Polyline& rail = state_.rails[j].curve;
        double sx = 0, sy = 0, sum = 0;
        int count = 0;
        for (int i = 0; i < m; ++i) {
            double s, u;
            if (closestApproach(rail, G[i].curve, s, u) <= tol) {
                crossings[j][i] = {true, u, s};
                sx += std::cos(2 * kPi * u);
                sy += std::sin(2 * kPi * u);
                sum += u;
                ++count;
            }
        }
        if (count == 0) continue;
        railUsed[j] = 1;
        double x = closed ? wrap01(std::atan2(sy, sx) / (2 * kPi)) : sum / count;
        if (!closed) {
            // Rails drawn on the open ends define the side boundaries exactly.
            if (x < 0.03) x = 0.0;
            if (x > 0.97) x = 1.0;
        }
        railX[j] = x;
    }

    // 4. Per guide column maps (rails warp the flow of free guides).
    std::vector<MonotoneMap> maps(m);
    for (int i = 0; i < m; ++i) {
        std::vector<MonotoneMap::Anchor> anchors;
        if (!G[i].isBorder())
            for (int j = 0; j < nr; ++j)
                if (railUsed[j] && crossings[j][i].valid) anchors.push_back({railX[j], crossings[j][i].u});
        if (anchors.empty()) maps[i].setIdentity(closed);
        else maps[i].setAnchors(anchors, closed);
    }
    auto guidePointAtX = [&](int i, double x) {
        // Border guides follow their own vertex distribution (x-space is the
        // first border's parameter space), free guides follow their maps.
        if (G[i].isBorder()) return G[i].curve.pointAt(x);
        return G[i].curve.pointAt(maps[i](x));
    };
    auto guideParamAtX = [&](int i, double x) { return G[i].isBorder() ? x : maps[i](x); };

    // 5. Column points on every guide.
    std::vector<std::vector<Vec3>> P(m, std::vector<Vec3>(cols));
    for (int i = 0; i < m; ++i)
        for (int k = 0; k < cols; ++k)
            P[i][k] = G[i].isBorder() ? G[i].curve.pts[k] : guidePointAtX(i, xk[k]);

    // 6. Rows per span.
    std::vector<int> V(m - 1, settings_.vSegments);
    double avgEdge = 0;
    for (int i = 0; i < m; ++i) avgEdge += G[i].curve.length() / U;
    avgEdge /= m;
    for (int i = 0; i + 1 < m; ++i) {
        if (settings_.autoV) {
            double d = 0;
            for (int k = 0; k < cols; ++k) d += distance(P[i][k], P[i + 1][k]);
            d /= cols;
            V[i] = static_cast<int>(std::lround(d / std::max(avgEdge, 1e-12)));
        }
        V[i] = std::max(1, std::min(V[i], settings_.maxVPerSpan));
    }

    // 7. Rail deviations per span (expressed in the span frame, scaled by span length).
    std::vector<std::vector<RailSpan>> railSpans(m - 1);
    for (int j = 0; j < nr; ++j) {
        if (!railUsed[j]) continue;
        const Polyline& rail = state_.rails[j].curve;
        std::vector<Vec3> cq(m);
        for (int i = 0; i < m; ++i) cq[i] = guidePointAtX(i, railX[j]);
        for (int i = 0; i + 1 < m; ++i) {
            if (!crossings[j][i].valid || !crossings[j][i + 1].valid) continue;
            const double sA = crossings[j][i].s, sB = crossings[j][i + 1].s;
            const Vec3 tangent = G[i].curve.tangentAt(guideParamAtX(i, railX[j])) +
                                 G[i + 1].curve.tangentAt(guideParamAtX(i + 1, railX[j]));
            const Frame f = spanFrame(cq[i], cq[i + 1], tangent);
            RailSpan rs;
            rs.x = railX[j];
            const int vi = V[i];
            std::vector<Vec3> dev(vi + 1);
            for (int r = 0; r <= vi; ++r) {
                const double t = static_cast<double>(r) / vi;
                dev[r] = rail.pointAt(lerp(sA, sB, t)) - columnBase(cq, i, t, settings_.smoothFlow);
            }
            for (int r = 0; r <= vi; ++r) {
                const double t = static_cast<double>(r) / vi;
                const Vec3 d = dev[r] - lerp(dev[0], dev[vi], t);  // guides stay exact
                rs.coeff.push_back(Vec3(dot(d, f.e1), dot(d, f.e2), dot(d, f.e3)) / f.len);
            }
            railSpans[i].push_back(std::move(rs));
        }
    }
    for (auto& list : railSpans)
        std::sort(list.begin(), list.end(), [](const RailSpan& a, const RailSpan& b) { return a.x < b.x; });

    auto railCoeff = [&](int span, double x, int r) -> Vec3 {
        const auto& list = railSpans[span];
        if (list.empty()) return Vec3();
        const int n = static_cast<int>(list.size());
        if (closed) {
            if (n == 1) return list[0].coeff[r];
            int b = 0;
            while (b < n && list[b].x <= x) ++b;
            const int a = (b - 1 + n) % n;
            b %= n;
            double gap = list[b].x - list[a].x;
            double off = x - list[a].x;
            if (gap <= 0) gap += 1.0;
            if (off < 0) off += 1.0;
            return lerp(list[a].coeff[r], list[b].coeff[r], clamp(off / gap, 0.0, 1.0));
        }
        if (x <= list.front().x) {
            const double f = list.front().x > 1e-9 ? smoothstep(x / list.front().x) : 1.0;
            return list.front().coeff[r] * f;
        }
        if (x >= list.back().x) {
            const double f = list.back().x < 1 - 1e-9 ? smoothstep((1 - x) / (1 - list.back().x)) : 1.0;
            return list.back().coeff[r] * f;
        }
        int b = 1;
        while (b < n && list[b].x < x) ++b;
        const auto& A = list[b - 1];
        const auto& B = list[b];
        const double gap = B.x - A.x;
        return lerp(A.coeff[r], B.coeff[r], gap > 1e-12 ? (x - A.x) / gap : 0.0);
    };

    // 8. Assemble the grid.
    BridgeGrid& grid = out.grid;
    grid.closedU = closed;
    grid.cols = cols;
    grid.rows = 1;
    for (int v : V) grid.rows += v;
    grid.pts.assign(static_cast<size_t>(grid.rows) * cols, Vec3());
    grid.rowGuide.assign(grid.rows, -1);
    grid.rowBorderVerts.assign(grid.rows, {});

    std::vector<char> fixedRow(grid.rows, 0);
    int row = 0;
    for (int i = 0; i + 1 < m; ++i) {
        for (int k = 0; k < cols; ++k) {
            std::vector<Vec3> q(m);
            for (int g = 0; g < m; ++g) q[g] = P[g][k];
            const double x = xk[k];
            const Vec3 tangent = G[i].curve.tangentAt(guideParamAtX(i, x)) +
                                 G[i + 1].curve.tangentAt(guideParamAtX(i + 1, x));
            const Frame f = spanFrame(q[i], q[i + 1], tangent);
            for (int r = 0; r < V[i]; ++r) {
                const double t = static_cast<double>(r) / V[i];
                Vec3 p = columnBase(q, i, t, settings_.smoothFlow);
                const Vec3 c = railCoeff(i, x, r);
                p += (f.e1 * c.x + f.e2 * c.y + f.e3 * c.z) * f.len;
                grid.at(row + r, k) = p;
            }
        }
        grid.rowGuide[row] = i;
        fixedRow[row] = 1;
        if (G[i].isBorder()) grid.rowBorderVerts[row] = G[i].border;
        row += V[i];
    }
    for (int k = 0; k < cols; ++k) grid.at(row, k) = P[m - 1][k];
    grid.rowGuide[row] = m - 1;
    fixedRow[row] = 1;
    if (G[m - 1].isBorder()) grid.rowBorderVerts[row] = G[m - 1].border;
    // Guide rows are exact guide points (already on the surface).
    for (int i = 0, r = 0; i < m; r += (i + 1 < m ? V[i] : 0), ++i)
        for (int k = 0; k < cols; ++k) grid.at(r, k) = P[i][k];

    // 9. Project interior rows onto the High Poly along the grid normal.
    const double offset = settings_.offset;
    const Surface& S = *surface_;
    const double search = S.scale() * 0.25;
    auto gridNormal = [&](int r, int c) {
        auto colAt = [&](int cc) {
            if (closed) return ((cc % cols) + cols) % cols;
            return std::max(0, std::min(cols - 1, cc));
        };
        const Vec3 du = grid.at(r, colAt(c + 1)) - grid.at(r, colAt(c - 1));
        const Vec3 dv = grid.at(std::min(grid.rows - 1, r + 1), c) - grid.at(std::max(0, r - 1), c);
        return normalize(cross(du, dv), Vec3());
    };
    auto projectRow = [&](int r) {
        std::vector<Vec3> normals(cols);
        for (int c = 0; c < cols; ++c) normals[c] = gridNormal(r, c);
        for (int c = 0; c < cols; ++c) {
            Vec3 q;
            const Vec3* hint = length2(normals[c]) > 0.5 ? &normals[c] : nullptr;
            if (S.project(grid.at(r, c), offset, q, hint, search) || S.project(grid.at(r, c), offset, q))
                grid.at(r, c) = q;
        }
    };
    for (int r = 0; r < grid.rows; ++r)
        if (!fixedRow[r]) projectRow(r);

    // 10. Even spacing along every column span, then a light tangential relax.
    if (settings_.evenSpacing) {
        for (int pass = 0; pass < 2; ++pass) {
            int r0 = 0;
            for (int i = 0; i + 1 < m; ++i) {
                const int r1 = r0 + V[i];
                if (V[i] > 1) {
                    for (int c = 0; c < cols; ++c) {
                        Polyline col;
                        for (int r = r0; r <= r1; ++r) col.pts.push_back(grid.at(r, c));
                        for (int r = r0 + 1; r < r1; ++r)
                            grid.at(r, c) = col.pointAt(static_cast<double>(r - r0) / V[i]);
                    }
                    for (int r = r0 + 1; r < r1; ++r) projectRow(r);
                }
                r0 = r1;
            }
        }
    }
    for (int it = 0; it < settings_.relaxIterations; ++it) {
        std::vector<Vec3> next(grid.pts);
        for (int r = 1; r < grid.rows - 1; ++r) {
            if (fixedRow[r]) continue;
            for (int c = 0; c < cols; ++c) {
                const bool sideBoundary = !closed && (c == 0 || c == cols - 1);
                const Vec3& p = grid.at(r, c);
                const Vec3 up = grid.at(r - 1, c), dn = grid.at(r + 1, c);
                Vec3 target;
                if (sideBoundary) {
                    target = (up + dn) * 0.5;
                } else {
                    const Vec3 lf = grid.at(r, (c - 1 + cols) % cols);
                    const Vec3 rt = grid.at(r, (c + 1) % cols);
                    target = (up + dn + lf + rt) * 0.25;
                }
                const Vec3 n = gridNormal(r, c);
                next[static_cast<size_t>(r) * cols + c] =
                    p + projectOnPlane(target - p, n) * settings_.relaxStrength;
            }
        }
        grid.pts.swap(next);
        for (int r = 0; r < grid.rows; ++r)
            if (!fixedRow[r]) projectRow(r);
    }

    // 11. Winding: faces should face along the surface normal.
    double agree = 0;
    const int quadCols = closed ? cols : cols - 1;
    for (int r = 0; r + 1 < grid.rows; ++r) {
        for (int c = 0; c < quadCols; ++c) {
            const int c1 = (c + 1) % cols;
            const Vec3 a = grid.at(r, c), b = grid.at(r, c1), d = grid.at(r + 1, c);
            const Vec3 center = (a + b + grid.at(r + 1, c1) + d) * 0.25;
            SurfaceHit hit;
            if (S.closestPoint(center, hit)) agree += dot(cross(b - a, d - a), hit.normal) > 0 ? 1 : -1;
        }
    }
    grid.flip = agree < 0;

    out.uSegments = U;
    out.vSegments = V;
    out.ok = true;
    std::ostringstream s;
    s << grid.quadCount() << " quads  (" << U << " x " << (grid.rows - 1) << ")";
    out.message = s.str();
    return out;
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------
CommitResult BridgeSession::commit(PolyMesh& mesh, const BridgePreview& preview, bool autoWeldEnabled,
                                   double weldThreshold, bool continueFromLast) {
    CommitResult res;
    if (!preview.ok) {
        res.message = preview.message.empty() ? "Nothing to commit" : preview.message;
        return res;
    }
    const BridgeGrid& g = preview.grid;
    std::vector<int> ids(static_cast<size_t>(g.rows) * g.cols, -1);
    std::vector<int> created;
    for (int r = 0; r < g.rows; ++r) {
        const auto& border = g.rowBorderVerts[r];
        for (int c = 0; c < g.cols; ++c) {
            int id;
            if (!border.empty() && static_cast<int>(border.size()) == g.cols &&
                border[c] >= 0 && border[c] < mesh.vertexCount()) {
                id = border[c];
            } else {
                id = mesh.addVertex(g.at(r, c));
                created.push_back(id);
            }
            ids[static_cast<size_t>(r) * g.cols + c] = id;
        }
    }
    const int quadCols = g.closedU ? g.cols : g.cols - 1;
    int faces = 0;
    for (int r = 0; r + 1 < g.rows; ++r) {
        for (int c = 0; c < quadCols; ++c) {
            const int c1 = (c + 1) % g.cols;
            std::vector<int> f = {ids[static_cast<size_t>(r) * g.cols + c],
                                  ids[static_cast<size_t>(r) * g.cols + c1],
                                  ids[static_cast<size_t>(r + 1) * g.cols + c1],
                                  ids[static_cast<size_t>(r + 1) * g.cols + c]};
            if (g.flip) std::reverse(f.begin(), f.end());
            mesh.addFace(std::move(f));
            ++faces;
        }
    }

    std::vector<int> remap;
    if (autoWeldEnabled && weldThreshold > 0) {
        res.welded = autoWeld(mesh, created, weldThreshold, &remap);
    } else {
        remap.resize(mesh.vertexCount());
        for (int v = 0; v < mesh.vertexCount(); ++v) remap[v] = v;
    }
    for (int v : created)
        if (remap[v] >= 0) res.newVerts.push_back(remap[v]);
    std::sort(res.newVerts.begin(), res.newVerts.end());
    res.newVerts.erase(std::unique(res.newVerts.begin(), res.newVerts.end()), res.newVerts.end());

    res.lastRow.closed = g.closedU;
    for (int c = 0; c < g.cols; ++c) {
        const int id = ids[static_cast<size_t>(g.rows - 1) * g.cols + c];
        res.lastRow.verts.push_back(remap[id]);
    }
    res.faces = faces;
    res.ok = true;
    {
        std::ostringstream s;
        s << "Committed " << faces << " quads";
        if (res.welded) s << ", welded " << res.welded << " vertices";
        res.message = s.str();
    }

    clear();
    if (continueFromLast) {
        bool valid = res.lastRow.verts.size() >= 2;
        for (int v : res.lastRow.verts) valid = valid && v >= 0;
        if (valid) addBorderGuide(mesh, res.lastRow);
    }
    return res;
}

}  // namespace rtp
