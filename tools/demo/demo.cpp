// TopoWeave offline demo.
//
// Simulates a user drawing splines in a viewport over a procedural character,
// runs the exact same engine the 3ds Max plug-in uses, and renders PNG images
// plus OBJ files of every step. Usage: topoweave_demo [output_dir]
#include "ObjIO.h"
#include "Render.h"
#include "Shapes.h"
#include "rtp/TopoWeave.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace rtp;

namespace {

std::string g_out = "demo_output";

const Surface* g_surface = nullptr;

// Control points are given as (x, _, z) positions on the front of the body:
// they are first dropped onto the visible surface (front orthographic ray) so
// the simulated mouse path lies over the body exactly like a user would draw.
Vec3 onBody(const Vec3& p) {
    SurfaceHit h;
    if (g_surface && g_surface->raycast(Ray(Vec3(p.x, -50, p.z), Vec3(0, 1, 0)), h)) return h.point;
    return p;
}

std::vector<std::pair<double, double>> screenPath(const kit::Camera& cam, const std::vector<Vec3>& ctrl) {
    std::vector<Vec3> pts;
    for (const auto& c : ctrl) pts.push_back(onBody(c));
    // Smooth the control points with Catmull-Rom like a hand-drawn stroke.
    std::vector<std::pair<double, double>> out;
    const int n = static_cast<int>(pts.size());
    for (int i = 0; i + 1 < n; ++i) {
        const Vec3 p0 = i > 0 ? pts[i - 1] : pts[i] * 2.0 - pts[i + 1];
        const Vec3 p3 = i + 2 < n ? pts[i + 2] : pts[i + 1] * 2.0 - pts[i];
        for (int k = (i == 0 ? 0 : 1); k <= 24; ++k) {
            const Vec3 p = catmullRom(p0, pts[i], pts[i + 1], p3, k / 24.0);
            double x, y, z;
            cam.project(p, x, y, z);
            out.push_back({x, y});
        }
    }
    return out;
}

Polyline draw(const Surface& s, const kit::Camera& cam, const std::vector<Vec3>& pts, double offset) {
    StrokeOptions opt;
    opt.offset = offset;
    const StrokeResult r = strokeFromRays(kit::strokeRays(cam, screenPath(cam, pts)), s, opt);
    if (!r.ok) std::printf("  stroke failed: %s\n", r.message);
    return r.curve;
}

Polyline contour(const Surface& s, const kit::Camera& cam, const Vec3& a, const Vec3& b, double offset) {
    ContourOptions opt;
    opt.offset = offset;
    opt.viewDir = cam.forward();
    std::vector<std::pair<double, double>> path;
    for (int i = 0; i <= 24; ++i) {
        double x, y, z;
        cam.project(lerp(a, b, i / 24.0), x, y, z);  // contour strokes cross the silhouette
        path.push_back({x, y});
    }
    const StrokeResult r = contourFromRays(kit::strokeRays(cam, path), s, opt);
    if (!r.ok) std::printf("  contour failed: %s\n", r.message);
    return r.curve;
}

PolyMesh previewMesh(const BridgeSession& session, const BridgePreview& p) {
    PolyMesh m;
    BridgeSession copy = session;
    copy.commit(m, p, false, 0.0, false);
    return m;
}

void render(const std::string& name, const kit::Camera& cam, const TriMesh& hp, const PolyMesh& mesh,
            const std::vector<Polyline>& guides, const std::vector<Polyline>& rails) {
    kit::Canvas c(cam);
    c.clearGradient({0.23, 0.25, 0.29}, {0.10, 0.11, 0.13});
    c.drawSurface(hp, {0.70, 0.70, 0.72});
    c.drawPolyFill(mesh, {1.0, 0.62, 0.20}, 0.30);
    c.drawPolyWire(mesh, {1.0, 0.55, 0.12}, 1.3);
    for (const auto& v : mesh.verts) c.drawPoint(v, {0.12, 0.08, 0.05}, 1.6);
    for (const auto& g : guides) c.drawPolyline(g, {0.25, 0.95, 0.35}, 2.6);
    for (const auto& r : rails) c.drawPolyline(r, {0.20, 0.80, 1.00}, 2.6);
    const std::string path = g_out + "/" + name + ".png";
    std::printf("  -> %s %s\n", path.c_str(), c.writePng(path) ? "" : "(write failed)");
}

void report(const char* title, const BridgePreview& p) {
    std::printf("%s: %s%s\n", title, p.ok ? "" : "FAILED - ", p.message.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) g_out = argv[1];
    std::filesystem::create_directories(g_out);
    std::printf("TopoWeave %s demo\n", kVersion);

    const TriMesh hp = kit::makeCharacter();
    Surface surface(hp);
    g_surface = &surface;
    kit::writeObj(g_out + "/highpoly.obj", hp);
    const double offset = 0.01;

    kit::Camera cam;
    cam.eye = Vec3(-2.6, -12.0, 2.8);
    cam.target = Vec3(0.3, 0.0, 0.8);
    cam.fovDeg = 38;

    // ---- 1. Two guides -> a conforming band ---------------------------------
    BridgeSession session;
    session.setSurface(&surface);
    session.settings().offset = offset;
    session.settings().uSegments = 12;
    session.addGuide(draw(surface, cam, {Vec3(-1.25, -3, 1.85), Vec3(0, -3, 2.1), Vec3(1.2, -3, 1.85)}, offset));
    session.addGuide(draw(surface, cam, {Vec3(1.35, -3, 0.5), Vec3(0, -3, 0.35), Vec3(-1.4, -3, 0.5)}, offset));
    BridgePreview p = session.build();
    report("01 two guides", p);
    std::vector<Polyline> guides = p.alignedGuides;
    render("01_two_guides", cam, hp, previewMesh(session, p), guides, {});

    // ---- 2. Rails customise the flow between the guides --------------------
    std::vector<Polyline> rails;
    rails.push_back(draw(surface, cam, {Vec3(-0.95, -3, 2.25), Vec3(-0.3, -3, 1.3), Vec3(-0.85, -3, 0.15)}, offset));
    rails.push_back(draw(surface, cam, {Vec3(0.6, -3, 2.3), Vec3(1.0, -3, 1.25), Vec3(0.5, -3, 0.15)}, offset));
    for (const auto& r : rails) {
        const StrokeRole role = session.addAuto(r);
        std::printf("  stroke classified as %s\n", role == StrokeRole::Rail ? "rail" : "guide");
    }
    p = session.build();
    report("02 rails", p);
    render("02_rails", cam, hp, previewMesh(session, p), p.alignedGuides, rails);

    // ---- 3. More guides -> more control over the band ----------------------
    BridgeSession many;
    many.setSurface(&surface);
    many.settings().offset = offset;
    many.settings().uSegments = 14;
    std::vector<Polyline> manyGuides = {
        draw(surface, cam, {Vec3(-1.3, -3, 1.9), Vec3(0, -3, 2.2), Vec3(1.25, -3, 1.9)}, offset),
        draw(surface, cam, {Vec3(-1.45, -3, -0.2), Vec3(-0.4, -3, 0.15), Vec3(1.45, -3, 0.45)}, offset),
        draw(surface, cam, {Vec3(-1.4, -3, 1.0), Vec3(0, -3, 0.95), Vec3(1.4, -3, 1.25)}, offset),
        draw(surface, cam, {Vec3(-1.35, -3, -1.3), Vec3(0, -3, -1.05), Vec3(1.35, -3, -1.3)}, offset),
    };
    for (const auto& g : manyGuides) many.addGuide(g);  // the 3rd stroke is inserted in between
    const Polyline centre = draw(surface, cam, {Vec3(-0.25, -3, 2.4), Vec3(0.3, -3, 0.6), Vec3(-0.1, -3, -1.5)}, offset);
    many.addAuto(centre);
    p = many.build();
    report("03 four guides + rail", p);
    render("03_four_guides", cam, hp, previewMesh(many, p), p.alignedGuides, {centre});

    // ---- 4. Contour rings around the arm (wrap the hidden back side) -------
    kit::Camera armCam;
    armCam.eye = Vec3(3.6, -9.5, 2.6);
    armCam.target = Vec3(3.6, 0, 1.6);
    armCam.fovDeg = 34;
    BridgeSession arm;
    arm.setSurface(&surface);
    arm.settings().offset = offset;
    arm.settings().uSegments = 12;
    const Vec3 a0(1.6, 0, 2.4), a1(5.6, 0, 1.2);
    const Vec3 axis = normalize(a1 - a0);
    const Vec3 across = normalize(Vec3(axis.z, 0, -axis.x)) * 1.3;
    std::vector<Polyline> rings;
    for (double t : {0.3, 0.5, 0.7, 0.88}) {
        const Vec3 c = lerp(a0, a1, t) + Vec3(0, -1.5, 0);
        rings.push_back(contour(surface, armCam, c + across, c - across, offset));
        arm.addGuide(rings.back());
    }
    p = arm.build();
    report("04 arm contours", p);
    render("04_arm_contours", armCam, hp, previewMesh(arm, p), p.alignedGuides, {});

    // ---- 5. A full workflow: commit, continue, bridge, conform ------------
    PolyMesh retopo;
    session.commit(retopo, session.build(), true, 0.02, true);  // chest band, continue from its last row
    session.addGuide(draw(surface, cam, {Vec3(-1.4, -3, -0.9), Vec3(0, -3, -0.75), Vec3(1.4, -3, -0.9)}, offset));
    p = session.build();
    report("05a continue", p);
    session.commit(retopo, p, true, 0.02, false);
    BridgeSession arms = arm;
    arms.commit(retopo, arms.build(), true, 0.02, false);
    BridgeSession left;
    left.setSurface(&surface);
    left.settings().offset = offset;
    left.settings().uSegments = 12;
    for (double t : {0.35, 0.6, 0.85}) {
        const Vec3 la0(-1.6, 0, 2.4), la1(-5.6, 0, 1.2);
        const Vec3 lax = normalize(la1 - la0);
        const Vec3 lacross = normalize(Vec3(lax.z, 0, -lax.x)) * 1.3;
        const Vec3 c = lerp(la0, la1, t) + Vec3(0, -1.5, 0);
        left.addGuide(contour(surface, cam, c + lacross, c - lacross, offset));
    }
    left.commit(retopo, left.build(), true, 0.02, false);
    RelaxOptions relax;
    relax.iterations = 2;
    relax.strength = 0.3;
    relax.offset = offset;
    relaxVertices(retopo, &surface, relax);
    const TopologyStats st = retopo.inspect();
    std::printf("05 workflow:\n%s", st.report().c_str());
    render("05_workflow", cam, hp, retopo, {}, {});
    kit::writeObj(g_out + "/retopo.obj", retopo);
    return 0;
}
