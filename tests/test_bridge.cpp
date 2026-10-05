#include "ObjIO.h"
#include "Shapes.h"
#include "Test.h"
#include "rtp/Bridge.h"
#include "rtp/Stroke.h"
#include "rtp/Surface.h"

#include <algorithm>

using namespace rtp;

namespace {

kit::Camera frontCamera(const Vec3& target = Vec3(0, 0, 0), double dist = 10) {
    kit::Camera cam;
    cam.eye = target + Vec3(0, -dist, 0);
    cam.target = target;
    return cam;
}

std::vector<std::pair<double, double>> screenPath(const kit::Camera& cam, const std::vector<Vec3>& pts,
                                                  int perSegment = 24) {
    std::vector<std::pair<double, double>> out;
    for (size_t i = 0; i + 1 < pts.size(); ++i)
        for (int k = (i == 0 ? 0 : 1); k <= perSegment; ++k) {
            double x, y, z;
            cam.project(lerp(pts[i], pts[i + 1], static_cast<double>(k) / perSegment), x, y, z);
            out.push_back({x, y});
        }
    return out;
}

Polyline drawOn(const Surface& s, const kit::Camera& cam, const std::vector<Vec3>& pts, double offset = 0.0) {
    StrokeOptions opt;
    opt.offset = offset;
    const StrokeResult r = strokeFromRays(kit::strokeRays(cam, screenPath(cam, pts)), s, opt);
    return r.ok ? r.curve : Polyline();
}

Polyline contourOn(const Surface& s, const kit::Camera& cam, const Vec3& a, const Vec3& b, double offset = 0.0) {
    ContourOptions opt;
    opt.offset = offset;
    opt.viewDir = cam.forward();
    const StrokeResult r = contourFromRays(kit::strokeRays(cam, screenPath(cam, {a, b})), s, opt);
    return r.ok ? r.curve : Polyline();
}

double maxSurfaceGap(const Surface& s, const BridgeGrid& g, double offset) {
    double worst = 0;
    for (const auto& p : g.pts) {
        SurfaceHit h;
        if (s.closestPoint(p, h)) worst = std::max(worst, std::fabs(h.t - offset));
    }
    return worst;
}

}  // namespace

TEST(bridge_needs_two_guides) {
    const Surface s(kit::makeTorso());
    BridgeSession session;
    session.setSurface(&s);
    CHECK(!session.build().ok);
    const auto cam = frontCamera();
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.0), Vec3(1.2, -3, 1.0)}));
    const BridgePreview p = session.build();
    CHECK(!p.ok);
    CHECK(p.message.find("second guide") != std::string::npos);
}

TEST(bridge_rings_on_limb) {
    const double offset = 0.02;
    const Surface s(kit::makeTube(Vec3(-4, 0, 0), Vec3(4, 0, 0), 1.0, 96, 48));
    const auto cam = frontCamera();
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 16;
    session.settings().offset = offset;
    const Polyline r1 = contourOn(s, cam, Vec3(-1.0, -2, 2), Vec3(-1.0, -2, -2), offset);
    const Polyline r2 = contourOn(s, cam, Vec3(1.0, -2, -2), Vec3(1.0, -2, 2), offset);  // drawn the other way
    REQUIRE(r1.valid() && r2.valid());
    session.addGuide(r1);
    session.addGuide(r2);
    const BridgePreview p = session.build();
    REQUIRE(p.ok);
    CHECK(p.grid.closedU);
    CHECK(p.grid.cols == 16);
    CHECK(p.grid.rows >= 3);
    double worst = 0;
    for (const auto& q : p.grid.pts) worst = std::max(worst, std::fabs(std::hypot(q.y, q.z) - (1.0 + offset)));
    CHECK(worst < 0.01);

    // No twisting: every column keeps (almost) the same angle along the limb.
    double worstTwist = 0;
    for (int c = 0; c < p.grid.cols; ++c) {
        const Vec3 a = p.grid.at(0, c), b = p.grid.at(p.grid.rows - 1, c);
        double d = std::fabs(std::atan2(a.z, a.y) - std::atan2(b.z, b.y));
        d = std::min(d, 2 * kPi - d);
        worstTwist = std::max(worstTwist, d);
    }
    CHECK(worstTwist < 0.05);

    PolyMesh mesh;
    const CommitResult cr = session.commit(mesh, p, true, 0.001, false);
    REQUIRE(cr.ok);
    CHECK(mesh.faceCount() == p.grid.quadCount());
    int outward = 0;
    for (int f = 0; f < mesh.faceCount(); ++f) {
        const Vec3 c = mesh.faceCenter(f);
        outward += dot(mesh.faceNormal(f), normalize(Vec3(0, c.y, c.z))) > 0;
    }
    CHECK(outward == mesh.faceCount());
    const TopologyStats st = mesh.inspect();
    CHECK(st.borderLoops == 2);
    CHECK(st.nonManifoldEdges == 0);
    kit::writeObj(tw_test::outputDir() + "/rings_on_limb.obj", mesh);
}

TEST(bridge_twist_rotates_ring) {
    const Surface s(kit::makeTube(Vec3(-4, 0, 0), Vec3(4, 0, 0), 1.0, 96, 48));
    const auto cam = frontCamera();
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 8;
    session.addGuide(contourOn(s, cam, Vec3(-1, -2, 2), Vec3(-1, -2, -2)));
    session.addGuide(contourOn(s, cam, Vec3(1, -2, 2), Vec3(1, -2, -2)));
    const BridgePreview a = session.build();
    session.setTwist(1, 0.25);
    const BridgePreview b = session.build();
    REQUIRE(a.ok && b.ok);
    const Vec3 pa = a.grid.at(a.grid.rows - 1, 0), pb = b.grid.at(b.grid.rows - 1, 0);
    double d = std::fabs(std::atan2(pa.z, pa.y) - std::atan2(pb.z, pb.y));
    d = std::min(d, 2 * kPi - d);
    CHECK_NEAR(d, kPi / 2, 0.05);
}

TEST(bridge_open_guides_conform_to_torso) {
    const double offset = 0.01;
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 10;
    session.settings().offset = offset;
    session.addGuide(drawOn(s, cam, {Vec3(-1.3, -3, 1.4), Vec3(0, -3, 1.6), Vec3(1.3, -3, 1.4)}, offset));
    session.addGuide(drawOn(s, cam, {Vec3(1.2, -3, -0.2), Vec3(-1.2, -3, -0.2)}, offset));  // reversed
    const BridgePreview p = session.build();
    REQUIRE(p.ok);
    CHECK(!p.grid.closedU);
    CHECK(p.grid.cols == 11);
    CHECK(maxSurfaceGap(s, p.grid, offset) < 0.01);

    // Auto rows give roughly square quads.
    double minRatio = kInf, maxRatio = 0;
    for (int r = 0; r + 1 < p.grid.rows; ++r)
        for (int c = 0; c + 1 < p.grid.cols; ++c) {
            const double du = distance(p.grid.at(r, c), p.grid.at(r, c + 1));
            const double dv = distance(p.grid.at(r, c), p.grid.at(r + 1, c));
            minRatio = std::min(minRatio, du / dv);
            maxRatio = std::max(maxRatio, du / dv);
        }
    CHECK(minRatio > 0.45);
    CHECK(maxRatio < 2.2);

    PolyMesh mesh;
    REQUIRE(session.commit(mesh, p, true, 0.001, false).ok);
    CHECK(mesh.orientToSurface(s) == 0);  // already facing outwards
    CHECK(mesh.inspect().borderLoops == 1);
    kit::writeObj(tw_test::outputDir() + "/torso_band.obj", mesh);
}

TEST(bridge_rail_redirects_flow) {
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 12;
    session.addGuide(drawOn(s, cam, {Vec3(-1.3, -3, 1.4), Vec3(1.3, -3, 1.4)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.3, -3, -0.4), Vec3(1.3, -3, -0.4)}));
    const Polyline rail = drawOn(s, cam, {Vec3(-0.7, -3, 1.7), Vec3(0.5, -3, -0.7)});
    REQUIRE(rail.valid());
    CHECK(session.classify(rail) == StrokeRole::Rail);
    session.addAuto(rail);
    CHECK(session.rails().size() == 1);
    const BridgePreview p = session.build();
    REQUIRE(p.ok);

    // Where the rail crosses each guide.
    double pa, pb;
    closestApproach(rail, p.alignedGuides.front(), pb, pa);
    const Vec3 top = p.alignedGuides.front().pointAt(pa);
    closestApproach(rail, p.alignedGuides.back(), pb, pa);
    const Vec3 bottom = p.alignedGuides.back().pointAt(pa);

    // Some column must start at the top crossing and end at the bottom crossing,
    // and all of its interior points must hug the rail.
    const double edge = p.alignedGuides.front().length() / 12;
    int bestCol = -1;
    double best = kInf;
    for (int c = 0; c < p.grid.cols; ++c) {
        const double d = distance(p.grid.at(0, c), top) + distance(p.grid.at(p.grid.rows - 1, c), bottom);
        if (d < best) { best = d; bestCol = c; }
    }
    CHECK(best < edge);
    double worst = 0;
    for (int r = 0; r < p.grid.rows; ++r) {
        double d;
        rail.closestParam(p.grid.at(r, bestCol), &d);
        worst = std::max(worst, d);
    }
    CHECK(worst < 0.6 * edge);

    // Without the rail, the column leaving the top crossing runs straight down
    // and misses the bottom crossing by far.
    session.removeLast();
    const BridgePreview plain = session.build();
    REQUIRE(plain.ok);
    int c0 = 0;
    for (int c = 1; c < plain.grid.cols; ++c)
        if (distance(plain.grid.at(0, c), top) < distance(plain.grid.at(0, c0), top)) c0 = c;
    CHECK(distance(plain.grid.at(plain.grid.rows - 1, c0), bottom) > 2 * edge);
}

TEST(bridge_guide_insertion_order) {
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.5), Vec3(1.2, -3, 1.5)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, -0.5), Vec3(1.2, -3, -0.5)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 0.5), Vec3(1.2, -3, 0.5)}));  // between
    REQUIRE(session.guides().size() == 3);
    CHECK(session.guides()[1].curve.centroid().z > 0.2);
    CHECK(session.guides()[1].curve.centroid().z < 0.8);
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 2.3), Vec3(1.2, -3, 2.3)}));  // above all
    CHECK(session.guides().front().curve.centroid().z > 2.0);
    const BridgePreview p = session.build();
    REQUIRE(p.ok);
    CHECK(p.vSegments.size() == 3);
    // Rows land exactly on every guide.
    int r = 0;
    for (size_t i = 0; i < p.vSegments.size(); ++i) {
        CHECK(p.grid.rowGuide[r] == static_cast<int>(i));
        r += p.vSegments[i];
    }
    CHECK(p.grid.rowGuide[r] == 3);
}

TEST(bridge_commit_and_continue_shares_vertices) {
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 8;
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.8), Vec3(1.2, -3, 1.8)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.0), Vec3(1.2, -3, 1.0)}));
    PolyMesh mesh;
    const BridgePreview p1 = session.build();
    REQUIRE(p1.ok);
    const CommitResult c1 = session.commit(mesh, p1, true, 0.001, true);
    REQUIRE(c1.ok);
    REQUIRE(session.guides().size() == 1);
    CHECK(session.guides()[0].isBorder());

    // Next stroke continues the band from the committed last row.
    session.settings().uSegments = 20;  // ignored: the border dictates the count
    session.addGuide(drawOn(s, cam, {Vec3(-1.3, -3, -0.2), Vec3(1.3, -3, -0.2)}));
    const BridgePreview p2 = session.build();
    REQUIRE(p2.ok);
    CHECK(p2.uSegments == 8);
    const int before = mesh.vertexCount();
    const CommitResult c2 = session.commit(mesh, p2, false, 0.0, true);
    REQUIRE(c2.ok);
    CHECK(mesh.vertexCount() == before + p2.grid.cols * (p2.grid.rows - 1));
    const TopologyStats st = mesh.inspect();
    CHECK(st.borderLoops == 1);
    CHECK(st.nonManifoldEdges == 0);
    CHECK(st.quads == st.faces);
    kit::writeObj(tw_test::outputDir() + "/continued_band.obj", mesh);
}

TEST(bridge_between_two_borders) {
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    session.settings().uSegments = 9;
    PolyMesh mesh;
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 2.0), Vec3(1.2, -3, 2.0)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.4), Vec3(1.2, -3, 1.4)}));
    REQUIRE(session.commit(mesh, session.build(), true, 0.001, false).ok);
    session.addGuide(drawOn(s, cam, {Vec3(-1.1, -3, 0.0), Vec3(1.1, -3, 0.0)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.1, -3, -0.6), Vec3(1.1, -3, -0.6)}));
    REQUIRE(session.commit(mesh, session.build(), true, 0.001, false).ok);
    CHECK(mesh.inspect().borderLoops == 2);

    // Pick the facing border spans by stroking along them.
    BorderChain a, b;
    REQUIRE(pickBorderSpan(mesh, drawOn(s, cam, {Vec3(-1.3, -3, 1.4), Vec3(1.3, -3, 1.4)}), 0.08, a));
    REQUIRE(pickBorderSpan(mesh, drawOn(s, cam, {Vec3(-1.2, -3, 0.0), Vec3(1.2, -3, 0.0)}), 0.08, b));
    CHECK(a.verts.size() == 10);
    CHECK(b.verts.size() == 10);
    session.addBorderGuide(mesh, a);
    session.addBorderGuide(mesh, b);
    const BridgePreview p = session.build();
    REQUIRE(p.ok);
    const int before = mesh.vertexCount();
    REQUIRE(session.commit(mesh, p, true, 0.001, false).ok);
    CHECK(mesh.vertexCount() == before + p.grid.cols * (p.grid.rows - 2));
    const TopologyStats st = mesh.inspect();
    CHECK(st.borderLoops == 1);
    CHECK(st.nonManifoldEdges == 0);
    CHECK(mesh.orientToSurface(s) == 0);
}

TEST(bridge_rejects_mismatched_borders) {
    const Surface s(kit::makeTorso());
    const auto cam = frontCamera(Vec3(0, 0, 0.5));
    BridgeSession session;
    session.setSurface(&s);
    PolyMesh mesh;
    session.settings().uSegments = 6;
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 2.0), Vec3(1.2, -3, 2.0)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.2, -3, 1.4), Vec3(1.2, -3, 1.4)}));
    REQUIRE(session.commit(mesh, session.build(), true, 0.001, false).ok);
    session.settings().uSegments = 7;
    session.addGuide(drawOn(s, cam, {Vec3(-1.1, -3, 0.0), Vec3(1.1, -3, 0.0)}));
    session.addGuide(drawOn(s, cam, {Vec3(-1.1, -3, -0.6), Vec3(1.1, -3, -0.6)}));
    REQUIRE(session.commit(mesh, session.build(), true, 0.001, false).ok);
    BorderChain a, b;
    REQUIRE(pickBorderSpan(mesh, drawOn(s, cam, {Vec3(-1.3, -3, 1.4), Vec3(1.3, -3, 1.4)}), 0.08, a));
    REQUIRE(pickBorderSpan(mesh, drawOn(s, cam, {Vec3(-1.2, -3, 0.0), Vec3(1.2, -3, 0.0)}), 0.08, b));
    session.addBorderGuide(mesh, a);
    session.addBorderGuide(mesh, b);
    const BridgePreview p = session.build();
    CHECK(!p.ok);
    CHECK(p.message.find("different") != std::string::npos);
}

TEST(bridge_closed_open_mismatch_is_reported) {
    const Surface s(kit::makeTube(Vec3(-4, 0, 0), Vec3(4, 0, 0), 1.0, 96, 48));
    const auto cam = frontCamera();
    BridgeSession session;
    session.setSurface(&s);
    session.addGuide(contourOn(s, cam, Vec3(-1, -2, 2), Vec3(-1, -2, -2)));
    session.addGuide(drawOn(s, cam, {Vec3(1, -3, 0.6), Vec3(1, -3, -0.6)}));
    const BridgePreview p = session.build();
    CHECK(!p.ok);
    CHECK(p.message.find("closed") != std::string::npos);
}
