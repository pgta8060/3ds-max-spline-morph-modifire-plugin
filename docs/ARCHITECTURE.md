# TopoWeave: architecture and algorithms

```
 3ds Max viewport                     core/ (SDK independent, unit tested)
 ─────────────────                    ─────────────────────────────────────
 mouse samples ──► rays (object space) ──► Stroke: strokeFromRays / contourFromRays
                                            │
 palette / hotkeys ──► BridgeSession ◄──────┘  guides, rails, border guides
                         │ build()  ──► BridgeGrid preview (drawn by Modifier::Display)
                         │ commit() ──► PolyMesh (authored topology)
 ModifyObject ◄──────────┘                    │
   PolyMesh ──► MNMesh ──► PolyObject output  └─ MeshOps: weld, relax, conform, brushes
```

The Max layer (`max/src`) only translates: mouse samples become rays, the
parameter block becomes `BridgeSettings`, and `PolyMesh` becomes an `MNMesh`.
All geometry is in `core/`, so it is identical in the plug-in, the tests and
the demo.

## Surface (`Surface.h`)

- The High Poly sources are merged into one triangle soup **in the retopology
  object's space**, so every tool works in the same space as the output.
- A median-split BVH (4 triangles per leaf) answers ray casts (Möller–Trumbore,
  double sided, nearest child first) and closest-point queries (Ericson's
  closest point on a triangle, with box-distance pruning).
- Area-weighted vertex normals give smooth normals for the Offset.
- `project(p, offset, dirHint)` shoots both ways along a direction hint (the
  grid normal) and falls back to the closest point. When the hit is much
  farther away than the closest surface point, the closest point wins, which
  keeps points from jumping across thin folds.
- `slice(plane)` cuts every triangle, keys cut points by mesh edge, and chains
  them into open or closed polylines (used by contours).

## Strokes (`Stroke.h`)

- **Surface stroke**: rays are cast; misses and depth discontinuities split
  the stroke (a discontinuity is a 3D step much larger than the lateral step of
  the two rays at that depth). The longest piece is kept, resampled by arc
  length, Laplacian-smoothed **on** the surface (smooth, reproject, repeat), and
  lifted by Offset. The Max layer decides closure in screen space.
- **Contour**: the cutting plane contains the stroke direction and the
  viewport's forward axis, and passes through the hit nearest to the middle of
  the stroke. The cut loop nearest to that hit becomes a ring with consistent
  winding around the plane normal.

## Spline Bridge (`Bridge.h`)

Given guides `G₀…Gₘ₋₁` and rails `R₀…Rₙ₋₁`:

1. **Alignment.** Each guide is aligned to its predecessor: open guides are
   reversed when that matches the end points better; closed guides get the
   reversal and start shift that minimise the summed squared distance to the
   previous ring, refined with a parabolic fit. User twist is applied on top.
   Border guides can only rotate by whole vertices.
2. **Columns.** `U` comes from the settings, or from the edge count of a border
   guide. With a border, the base column distribution is the border's own
   vertex distribution, so the band welds exactly onto it.
3. **Rail crossings.** Each rail is intersected with each guide by
   segment-segment closest approach, within a tolerance derived from the guide
   spacing. Its column parameter `x` is the (circular, for loops) mean of its
   crossing parameters.
4. **Flow maps.** Each free guide gets a monotone map `u = Mᵢ(x)` through its
   rail anchors (Fritsch–Carlson monotone cubic, periodic for loops). Rails
   therefore warp *which point of each guide forms a column* without letting
   columns cross.
5. **Rows.** Rows per span are either fixed or `round(span width / column
   width)`, which keeps quads square.
6. **Base surface.** Each column is a centripetal Catmull-Rom through its
   points on all guides, with reflected phantom points at the ends. With two
   guides this reduces to a straight blend.
7. **Rail deviations.** Along each rail, the difference between the rail and
   the base column at the rail's `x` is expressed in a local span frame
   (across, along the guides, normal) and scaled by the span length. The
   endpoint error is removed so guide rows stay exact. Columns between rails
   blend these coefficients linearly (periodically for loops). On open bands
   they fade out towards the free ends.
8. **Projection.** Interior rows are projected onto the High Poly along the grid
   normal (central differences), then lifted by Offset.
9. **Even spacing and relax.** Each column span is re-sampled uniformly by arc
   length and re-projected (two passes). Then a light tangential Laplacian
   relaxes the interior; side boundaries only slide along their columns.
10. **Winding.** Quad normals are compared with the surface normal at the quad
    centre; the majority decides whether faces are flipped.

`commit()` writes the grid into the `PolyMesh`, reusing border vertices, and
runs the operation-scoped Auto Weld on the new vertices. With *Continue after
commit*, the session restarts with the last row as a border guide.

## Mesh operations (`MeshOps.h`)

- **Auto Weld**: union-find over source vertices only, using a spatial hash.
  Existing vertices win over new ones, and merges that would collapse a face
  edge are rejected. Only merged vertices are removed.
- **Relax**: tangential Laplacian. Border vertices slide along the border
  tangent; corners (angle sharper than about 135°) stay pinned. Frozen faces
  protect their vertices everywhere.
- **Brushes**: smooth `(1-(d/r)²)²` falloff footprint; Move translates then
  re-conforms.
- **pickBorderSpan**: picks the border loop with the most vertices near the
  stroke, then its longest contiguous run (a click returns the whole loop).

## 3ds Max integration (`max/src`)

| File | Responsibility |
|---|---|
| `TopoWeaveMod.*` | Modifier: owns the `PolyMesh`, outputs a PolyObject, caches the Surface, display, persistence |
| `Tools.*` | Command modes and mouse procs; gesture classification by modifier keys |
| `Restore.h` | `RestoreObj` snapshotting the mesh and the stroke session |
| `Params.cpp` | ClassDesc2, ParamBlockDesc2 (auto UI), rollout buttons, High Poly validation |
| `Palette.*` | Floating palette owned by the active modifier |
| `Actions.*` | Hotkey action table activated only while TopoWeave is being edited |
| `TopoWeave.rc` | Rollout, palette, accelerators, strings, version info |

Design choices:

- **Modifier-owned topology.** The first evaluation adopts the input mesh;
  afterwards the modifier outputs its own topology. Parameter changes never
  invalidate the pipeline (`REF_STOP`), only edits do.
- **Undo** snapshots the mesh. This is simple and robust, and fine for
  retopology-sized meshes (tens of thousands of faces).
- **Display** uses the legacy `GraphicsWindow` path, which Nitrous still calls
  for the current modifier. Polylines are chunked to the 32-point limit.
