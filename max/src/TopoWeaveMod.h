// TopoWeave for 3ds Max - the retopology modifier.
//
// Architecture
//   * The modifier owns the authored low-poly topology (rtp::PolyMesh) in the
//     object space of the node it is applied to. ModifyObject simply outputs
//     it as a PolyObject, so everything above it in the stack (TurboSmooth,
//     Symmetry, ...) works as usual.
//   * The High Poly sources live in the parameter block (INode table). They
//     are merged into one rtp::Surface (BVH) in the retopology object space
//     and cached until a source, its transform or the edited node changes.
//   * Tools are 3ds Max command modes. Every mouse sample becomes a ray in
//     object space and is handed to the SDK independent core (rtp::*).
//   * Every committed edit is wrapped in theHold with a full snapshot of the
//     mesh and of the stroke session, so native Undo / Redo just works.
#pragma once

#include "SdkCompat.h"
#include "resource.h"

#include <iparamb2.h>
#include <iparamm2.h>
#include <max.h>
#include <polyobj.h>

#include "rtp/TopoWeave.h"

#include <string>
#include <vector>

extern HINSTANCE hInstance;
const MCHAR* GetString(int id);
ClassDesc2* GetTopoWeaveDesc();

#define TOPOWEAVE_CLASS_ID Class_ID(0x5a3c7e21, 0x1b9d4f60)

enum { tw_params };

enum TWParamID {
    pb_hp_nodes,
    pb_offset,
    pb_live_conform,
    pb_auto_weld,
    pb_weld_threshold,
    pb_show_cage,
    pb_cage_color,
    pb_u_segments,
    pb_auto_v,
    pb_v_segments,
    pb_relax_iters,
    pb_continue,
    pb_brush_radius,
    pb_brush_strength,
    pb_smooth_flow,
};

enum TWTool {
    TW_TOOL_NONE = 0,
    TW_TOOL_BRIDGE,
    TW_TOOL_RELAX,
    TW_TOOL_MOVE,
    TW_TOOL_CONFORM,
    TW_TOOL_COUNT
};

class TWCommandMode;
class TWPalette;

class TopoWeaveMod : public Modifier {
public:
    enum { PBLOCK_REF = 0 };

    TopoWeaveMod();
    ~TopoWeaveMod() override;

    // ---- Animatable ------------------------------------------------------
    Class_ID ClassID() override { return TOPOWEAVE_CLASS_ID; }
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    TW_GETCLASSNAME_SIG { s = GetString(IDS_CLASS_NAME); }
    void DeleteThis() override { delete this; }
    int NumSubs() override { return 1; }
    Animatable* SubAnim(int i) override { return i == 0 ? pblock : nullptr; }
    TW_SUBANIMNAME_SIG { return i == 0 ? MSTR(GetString(IDS_PARAMS)) : MSTR(); }
    int NumParamBlocks() override { return 1; }
    IParamBlock2* GetParamBlock(int i) override { return i == 0 ? pblock : nullptr; }
    IParamBlock2* GetParamBlockByID(BlockID id) override { return pblock->ID() == id ? pblock : nullptr; }

    // ---- ReferenceMaker / Target -----------------------------------------
    int NumRefs() override { return 1; }
    RefTargetHandle GetReference(int i) override { return i == PBLOCK_REF ? pblock : nullptr; }
    RefTargetHandle Clone(RemapDir& remap) override;
    RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget, PartID& partID,
                               RefMessage message, BOOL propagate) override;
    IOResult Save(ISave* isave) override;
    IOResult Load(ILoad* iload) override;

    // ---- BaseObject ------------------------------------------------------
    CreateMouseCallBack* GetCreateMouseCallBack() override { return nullptr; }
    TW_GETOBJECTNAME_SIG { return GetString(IDS_CLASS_NAME); }
    void BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) override;
    void EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) override;
    int Display(TimeValue t, INode* inode, ViewExp* vpt, int flags, ModContext* mc) override;
    void GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* vpt, Box3& box, ModContext* mc) override;

    // ---- Modifier --------------------------------------------------------
    Interval LocalValidity(TimeValue) override { return FOREVER; }
    ChannelMask ChannelsUsed() override { return GEOM_CHANNEL | TOPO_CHANNEL; }
    ChannelMask ChannelsChanged() override {
        return GEOM_CHANNEL | TOPO_CHANNEL | SELECT_CHANNEL | TEXMAP_CHANNEL | VERTCOLOR_CHANNEL;
    }
    Class_ID InputType() override { return polyObjectClassID; }
    BOOL ChangeTopology() override { return TRUE; }
    void ModifyObject(TimeValue t, ModContext& mc, ObjectState* os, INode* node) override;

    // ---- TopoWeave API (used by tools, palette and rollout) --------------
    bool IsEditing() const { return ip_ != nullptr; }
    IObjParam* Interface() const { return ip_; }
    INode* EditNode();
    // Rebuilds the High Poly surface if anything changed. False if empty.
    bool EnsureSurface(TimeValue t);
    const rtp::Surface& Surface() const { return surface_; }
    bool HasSurface() const { return !surface_.empty(); }

    // World <-> retopology object space.
    Matrix3 ObjectTM(TimeValue t);
    rtp::Ray WorldRayToObject(const Ray& r, TimeValue t);

    // Tools
    void SetTool(int tool);       // toggles when the tool is already active
    int ActiveTool() const { return activeTool_; }
    void OnToolEntered(int tool);
    void OnToolExited(int tool);

    // Bridge session
    rtp::BridgeSession& Session() { return session_; }
    const rtp::BridgePreview& Preview();
    void InvalidatePreview() { previewValid_ = false; }
    void SyncSessionSettings();
    void AddStroke(const rtp::Polyline& curve, int forcedRole /* -1 auto, 0 guide, 1 rail */);
    bool AddBorderGuide(const rtp::Polyline& stroke, double tolerance);
    bool DeleteStrokeNear(const rtp::Vec3& p, double tolerance);
    void RemoveLastStroke();
    void ClearStrokes();
    bool CommitBridge(bool keepToolActive);

    // Undo scopes. Each Begin* opens theHold with the right snapshot, the
    // matching End* accepts it, CancelMeshEdit rolls back whatever is open.
    rtp::PolyMesh& Authored() { return mesh_; }
    void BeginMeshEdit();                         // mesh + strokes snapshot
    void EndMeshEdit(const MCHAR* undoName);
    void BeginSessionEdit();                      // strokes only
    void EndSessionEdit(const MCHAR* undoName);
    void BeginParamEdit();                        // parameter block changes
    void EndParamEdit(const MCHAR* undoName);
    void CancelMeshEdit();

    // Mesh level operations (each one is a single undoable step)
    void ConformAll();
    void RelaxAll();
    void ShowInspector(HWND parent);
    void RepairMesh();
    void SnapToWorldZero(int axis);
    void ResetFromInput();
    void ClearMesh();
    void MeshChanged();                           // notify dependents + redraw

    // Viewport feedback drawn by Display()
    struct Feedback {
        // The live stroke and the hint are placed in object space along the
        // camera rays (just in front of the surface), so no window-coordinate
        // conventions are involved.
        std::vector<Point3> stroke;
        bool strokeIsRail = false;
        bool brushVisible = false;
        Point3 brushCenter;
        Point3 brushNormal;
        float brushRadius = 0;
        HWND hintWnd = nullptr;
        Point3 hintAnchor;
        bool hintAnchorValid = false;
        std::wstring hint;
    } feedback;
    void RequestRedraw(bool interactive);

    // Palette
    void ShowPalette(bool show);
    void TogglePalette();
    void UpdatePalette();
    void SetStatus(const std::wstring& text);
    const std::wstring& Status() const { return status_; }

    // Parameter shortcuts
    float Offset();
    float BrushRadius();
    float BrushStrength();
    void SetBrushRadius(float r);
    void SetBrushStrength(float s);

    IParamBlock2* pblock = nullptr;

private:
    void SetReference(int i, RefTargetHandle rtarg) override;
    void ImportFrom(MNMesh& mm);
    void WriteTo(MNMesh& mm) const;

    rtp::PolyMesh mesh_;
    bool meshInitialized_ = false;
    bool reimportRequested_ = false;

    rtp::Surface surface_;
    bool surfaceDirty_ = true;
    Matrix3 surfaceSelfTM_;
    std::vector<INode*> surfaceNodes_;

    rtp::BridgeSession session_;
    rtp::BridgePreview preview_;
    bool previewValid_ = false;

    // Pending undo snapshot (BeginMeshEdit / EndMeshEdit)
    bool holdOpen_ = false;

    std::wstring status_;
    int activeTool_ = TW_TOOL_NONE;

    IObjParam* ip_ = nullptr;
    TWCommandMode* modes_[TW_TOOL_COUNT] = {};
    TWPalette* palette_ = nullptr;

    friend class TWMeshRestore;
    friend class TWParamDlgProc;
};

// Conversions between Max and core types.
inline rtp::Vec3 ToVec(const Point3& p) { return rtp::Vec3(p.x, p.y, p.z); }
inline Point3 ToPoint(const rtp::Vec3& v) {
    return Point3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}
