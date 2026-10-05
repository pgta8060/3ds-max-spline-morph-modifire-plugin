# TopoWeave: user guide

## 1. Set up

1. Create the object that will hold the retopology: an **Editable Poly** with
   no faces works well (create a Plane, convert it to Editable Poly, delete all
   faces). An existing partial retopology works too, and TopoWeave adopts it.
2. Add the **TopoWeave** modifier (category *TopoWeave*).
3. In **High Poly sources**, use **Pick** (viewport) or **Add...** (by name) to
   assign one or more reference meshes. Tools stay unavailable until at least
   one source is assigned.
4. Set **Offset** to a small positive value (around 0.1–0.5% of the character's
   size). Projected points then float just above the surface, which avoids
   z-fighting.

The floating **TopoWeave palette** opens automatically while the modifier is
the current entry of the modify panel, and closes when you select anything
else. **Open TopoWeave Palette** in the rollout is a manual fallback.

> The authored topology lives inside the modifier and is saved with the scene.
> Uncommitted strokes are not saved; commit before closing the file.

## 2. Spline Bridge (key `1`)

### Guides: the shape of the band

Drag with the left mouse button **along** the body. The first stroke is a
guide (green). A second guide creates the preview band (amber grid). Every
further guide:

- drawn beyond the ends **extends** the band;
- drawn between two guides is **inserted** between them;
- bends the flow smoothly through all guides (turn off **Smooth flow** for
  straight spans between guides).

Strokes that end close to where they started (on screen) become **closed
loops**, for example a loop around the eye or the mouth.

### Rails: the flow inside the band

A stroke that **crosses two guides** is recognised as a rail (cyan). Hold
**Shift** to force a rail. A rail pins *which point of one guide connects to
which point of the next*, and the quad columns follow its exact path. With
several rails, their influence blends smoothly. With open guides, a rail's
influence fades towards the open ends unless you draw rails there as well.
Drawing rails along both ends defines the side boundaries exactly.

### Contours: rings around limbs

Hold **Alt** and drag **across** an arm, leg, finger or the torso. TopoWeave
cuts the High Poly with the plane under your stroke and keeps the loop under
the cursor, so you get a full ring including the back side. Draw a few rings
along the limb to get a tube. If the columns spiral, use
**Shift + Alt + drag** to twist the last ring.

### Continue or bridge existing topology

- **Ctrl + Shift + drag** along an open border uses that span as a guide. Its
  vertices are reused and welded, not duplicated.
- **Ctrl + Shift + click** uses the complete border loop (for example the open
  end of an arm tube).
- Load two borders to **bridge** them. Both must have the same number of
  edges; the status text tells you if they don't.
- With **Continue after commit** on (the default), the last row of every
  committed band automatically becomes the first guide of the next one.

### Density

| Gesture | Effect |
|---|---|
| Ctrl + drag left/right | Segments along the guides (borders dictate this when used) |
| Ctrl + Alt + drag | Rows per span (turns *Auto rows* off) |
| *Auto rows (square quads)* | Rows derived from the spacing, so quads stay square |
| *Relax* | Light smoothing passes after projection |

### Commit, edit, undo

- **Double-click**, **Enter** or **right-click** commits the band.
- **Ctrl + Shift + Alt + click** a stroke to delete it. *Remove last stroke*
  and *Clear strokes* are on the palette.
- **Ctrl + Z** undoes the last stroke, adjustment or commit, using 3ds Max's
  own undo stack.

## 3. Brushes

| Key | Brush | What it does |
|---|---|---|
| `2` | Relax | Evens out vertex spacing, keeps borders sliding along themselves |
| `3` | Move | Drags vertices across the surface with a soft falloff |
| `4` | Conform | Snaps vertices back onto the High Poly (+ Offset) |

- **Ctrl + Alt + drag** changes the strength.
- **Ctrl + Shift + Alt + drag** changes the radius.
- With **Auto Weld** on, vertices moved by the Move brush weld to nearby
  vertices when the stroke ends. Untouched vertices never move.

## 4. Mesh tools (rollout)

| Button | Effect |
|---|---|
| Conform All | Projects every vertex onto the High Poly |
| Relax All | Relaxes the whole mesh (borders slide, corners stay) |
| Inspect | Counts of quads, triangles, n-gons, borders, isolated vertices, non-manifold edges and degenerate faces |
| Repair | Removes degenerate faces, repeated indices and isolated vertices |
| Snap to world 0: X / Y / Z | Snaps vertices within the weld threshold of the **world** plane exactly onto it. Works on transformed objects, no Reset XForm needed |
| Reset from input | Re-adopts the modifier's input topology |
| Clear | Deletes all authored topology (undoable) |

**Live Conform**: when switched on, the whole mesh is conformed immediately,
and committed bands are re-conformed.

## 5. Working under TurboSmooth

Add TurboSmooth (or any modifier) above TopoWeave, then enable *Show End
Result* in the modify panel and **Show Cage** in TopoWeave. The authored cage
is drawn in the cage colour (click the swatch to change it) over the smoothed
result.

## 6. Troubleshooting

| Problem | Check |
|---|---|
| TopoWeave not in the modifier list | The `.dlm` must be built for your 3ds Max release and copied directly into `Plugins\`. Restart Max |
| Tools do nothing / "Assign a High Poly source first" | Add a source with Pick or Add... |
| The band flips or folds | Draw guides in a consistent order, or add a rail to pin the flow |
| Rings spiral along a limb | Shift + Alt + drag to twist the last ring |
| "Borders have different edge counts" | Use borders with the same number of edges, or bridge free guides instead |
| Number keys do nothing | TopoWeave must be the selected modifier. Check *Customize > Hotkey Editor > TopoWeave* |
| A stroke is rejected | It must start on the High Poly. Contours must cross the limb |
