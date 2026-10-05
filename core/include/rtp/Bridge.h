// TopoWeave core - Spline Bridge: projected quad bands from drawn splines.
//
// The user draws splines on the High Poly:
//
//   * Guides run *along* the band (like edge loops). Two guides produce a
//     band of quads between them; every additional guide extends the band and
//     bends its flow smoothly through all guides (Catmull-Rom across guides).
//   * Rails run *across* the guides. A rail pins which point of one guide
//     flows into which point of the next (it warps the column flow) and gives
//     the interior of the span its exact shape. Each extra rail customises the
//     band further; between rails the influence is blended smoothly.
//
// The interpolated grid is a Gordon-style surface built from that curve
// network. Every interior point is then projected onto the High Poly along
// the grid normal, lifted by Offset, and the spacing along each column is
// equalised so quads stay even on curved anatomy.
//
// Guides may also come from existing open borders of the retopology mesh: the
// band then reuses those exact vertices and welds seamlessly, which is how
// the tool continues an existing mesh or bridges two borders.
#pragma once

#include "rtp/MeshOps.h"
#include "rtp/MonotoneMap.h"
#include "rtp/PolyMesh.h"
#include "rtp/Polyline.h"

#include <string>
#include <vector>

namespace rtp {

class Surface;

enum class StrokeRole { Guide, Rail };

struct BridgeSettings {
    int uSegments = 12;            // segments along the guides (borders override)
    bool autoV = true;             // derive rows per span from spacing (square quads)
    int vSegments = 4;             // rows per span when autoV is false
    double offset = 0.0;           // lift above the High Poly
    bool smoothFlow = true;        // smooth (Catmull-Rom) flow across >2 guides
    int relaxIterations = 2;       // light smoothing after projection
    double relaxStrength = 0.35;
    bool evenSpacing = true;       // equalise spacing along columns after projection
    double railTolerance = 0.0;    // max rail/guide gap to count as crossing (0 = auto)
    int maxVPerSpan = 200;
};

struct Guide {
    Polyline curve;
    std::vector<int> borderVerts;  // non-empty: guide is an existing mesh border
    double twist = 0.0;            // closed guides: start shift as a fraction of the loop
    int id = 0;
    bool isBorder() const { return !borderVerts.empty(); }
};

struct Rail {
    Polyline curve;
    int id = 0;
};

// The previewed band. Rows run along the guides, columns across them.
struct BridgeGrid {
    int rows = 0;                 // number of vertex rows
    int cols = 0;                 // unique vertex columns (closed: no seam duplicate)
    bool closedU = false;         // rows are closed loops
    bool flip = false;            // face winding must be flipped to face outwards
    std::vector<Vec3> pts;        // rows * cols
    std::vector<int> rowGuide;    // guide index for guide rows, -1 for interior rows
    std::vector<std::vector<int>> rowBorderVerts;  // mesh vertex ids for border rows

    const Vec3& at(int r, int c) const { return pts[static_cast<size_t>(r) * cols + c]; }
    Vec3& at(int r, int c) { return pts[static_cast<size_t>(r) * cols + c]; }
    int quadCount() const { return (rows - 1) * (closedU ? cols : cols - 1); }
};

struct BridgePreview {
    bool ok = false;
    std::string message;
    BridgeGrid grid;
    std::vector<Polyline> alignedGuides;  // guides after direction/start alignment
    int uSegments = 0;
    std::vector<int> vSegments;           // per span
};

struct CommitResult {
    bool ok = false;
    std::string message;
    std::vector<int> newVerts;    // created vertices (after weld/compact)
    BorderChain lastRow;          // vertices of the last guide row (to continue from)
    int faces = 0;
    int welded = 0;
};

class BridgeSession {
public:
    struct State {
        std::vector<Guide> guides;
        std::vector<Rail> rails;
        int nextId = 1;
    };

    void setSurface(const Surface* surface) { surface_ = surface; }
    const Surface* surface() const { return surface_; }

    BridgeSettings& settings() { return settings_; }
    const BridgeSettings& settings() const { return settings_; }

    const std::vector<Guide>& guides() const { return state_.guides; }
    const std::vector<Rail>& rails() const { return state_.rails; }
    bool hasStrokes() const { return !state_.guides.empty() || !state_.rails.empty(); }

    // Adds a guide, inserted between existing guides when it lies between them.
    int addGuide(const Polyline& curve);
    // Adds an existing border chain as a guide (its vertices are reused on commit).
    int addBorderGuide(const PolyMesh& mesh, const BorderChain& chain);
    int addRail(const Polyline& curve);
    // Rail if the stroke crosses at least two guides, otherwise guide.
    StrokeRole classify(const Polyline& curve) const;
    StrokeRole addAuto(const Polyline& curve);

    // Nearest stroke to p within tolerance; returns false if none.
    bool nearestStroke(const Vec3& p, double tolerance, StrokeRole& role, int& index) const;
    void removeStroke(StrokeRole role, int index);
    void removeLast();
    void clear();
    void setTwist(int guideIndex, double twist);

    // Builds the band from the current strokes. Cheap enough for live preview.
    BridgePreview build() const;

    // Writes the preview into the mesh. On success the session is reset and,
    // if `continueFromLast`, the last guide row becomes a border guide so the
    // next stroke extends the band seamlessly.
    CommitResult commit(PolyMesh& mesh, const BridgePreview& preview, bool autoWeldEnabled,
                        double weldThreshold, bool continueFromLast);

    const State& state() const { return state_; }
    void restore(const State& s) { state_ = s; }

    double railTolerance() const;

private:
    State state_;
    BridgeSettings settings_;
    const Surface* surface_ = nullptr;
};

}  // namespace rtp
