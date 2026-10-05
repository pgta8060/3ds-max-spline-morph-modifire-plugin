// TopoWeave core - editing operations on the authored mesh.
//
// Every operation honours Frozen Faces: a vertex touching a frozen face never
// moves and is never welded away.
#pragma once

#include "rtp/PolyMesh.h"
#include "rtp/Polyline.h"

#include <vector>

namespace rtp {

class Surface;

// ---- Auto Weld -------------------------------------------------------------
// Operation scoped weld: only `sources` (vertices created or moved by the
// current operation) may be merged, each into the nearest other vertex within
// `threshold`. Untouched vertices never move. A merge that would collapse an
// edge of a face (both ends in the same face) is skipped. Merged vertices are
// removed; `remap` (optional) receives old -> new indices for every vertex.
int autoWeld(PolyMesh& mesh, const std::vector<int>& sources, double threshold,
             std::vector<int>* remap = nullptr);

// ---- Projection / smoothing --------------------------------------------------
// Projects vertices onto the surface (+offset along its normal). Empty `subset`
// means all vertices. Returns the number of vertices moved.
int conformVertices(PolyMesh& mesh, const Surface& surface, double offset,
                    const std::vector<int>& subset = {});

struct RelaxOptions {
    int iterations = 5;
    double strength = 0.5;
    bool keepBorders = true;   // border vertices only slide along the border
    bool reproject = true;     // project back to the surface after each pass
    double offset = 0.0;
};
// Tangential Laplacian relax. `weights` (optional, per vertex in [0,1]) scale
// the effect, e.g. a brush falloff. Empty subset = all vertices.
int relaxVertices(PolyMesh& mesh, const Surface* surface, const RelaxOptions& opt,
                  const std::vector<int>& subset = {}, const std::vector<double>& weights = {});

// ---- Brushes ----------------------------------------------------------------
struct BrushSample {
    int vertex;
    double weight;  // smooth falloff, 1 at centre, 0 at radius
};
std::vector<BrushSample> brushFootprint(const PolyMesh& mesh, const Vec3& center, double radius);

// Moves vertices in the footprint by delta * weight, then conforms them.
void moveBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
               double radius, const Vec3& delta, double strength);
void relaxBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
                double radius, double strength);
void conformBrush(PolyMesh& mesh, const Surface& surface, double offset, const Vec3& center,
                  double radius);

// ---- Symmetry ---------------------------------------------------------------
// Snaps vertices within `threshold` of the plane axis == value (in the space
// the mesh is expressed in) exactly onto the plane. Returns vertices snapped.
int snapToPlane(PolyMesh& mesh, int axis, double value, double threshold);

// ---- Borders ----------------------------------------------------------------
// Finds the open border span that a stroke runs along. The nearest border
// loop is chosen and the longest contiguous run of its vertices lying within
// `tolerance` of the stroke is returned. When the stroke is very short (a
// click) the complete loop is returned instead.
bool pickBorderSpan(const PolyMesh& mesh, const Polyline& stroke, double tolerance,
                    BorderChain& out);

// Polyline through the positions of a border chain.
Polyline chainToPolyline(const PolyMesh& mesh, const BorderChain& chain);

}  // namespace rtp
