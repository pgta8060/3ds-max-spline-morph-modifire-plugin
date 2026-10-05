#include "TopoWeaveMod.h"

#include "Actions.h"
#include "Palette.h"
#include "Restore.h"
#include "Tools.h"

#include <triobj.h>

#include <algorithm>

namespace {

enum ChunkIds : USHORT {
    CHUNK_MESH = 0x2000,
    CHUNK_STATE = 0x2010,
};

std::wstring Widen(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (unsigned char c : s) w.push_back(static_cast<wchar_t>(c));
    return w;
}

// GraphicsWindow::polyline / wPolyline accept at most 32 points per call, so
// longer lines are drawn as overlapping chunks.
constexpr size_t kMaxGWPoints = 32;

template <class P, class F>
void DrawChunked(std::vector<P> pts, bool closed, F&& draw) {
    if (pts.size() < 2) return;
    if (closed) pts.push_back(pts.front());
    for (size_t start = 0; start + 1 < pts.size(); start += kMaxGWPoints - 1) {
        const size_t n = (std::min)(kMaxGWPoints, pts.size() - start);
        draw(static_cast<int>(n), &pts[start]);
    }
}

void DrawLine3D(GraphicsWindow* gw, std::vector<Point3> pts, bool closed) {
    DrawChunked(std::move(pts), closed,
                [gw](int n, Point3* p) { gw->polyline(n, p, nullptr, FALSE, nullptr); });
}

void DrawLineScreen(GraphicsWindow* gw, std::vector<IPoint3> pts) {
    DrawChunked(std::move(pts), false,
                [gw](int n, IPoint3* p) { gw->wPolyline(n, p, nullptr, FALSE, nullptr); });
}

void DrawPolyline(GraphicsWindow* gw, const rtp::Polyline& line) {
    std::vector<Point3> pts;
    pts.reserve(line.pts.size());
    for (const auto& p : line.pts) pts.push_back(ToPoint(p));
    DrawLine3D(gw, std::move(pts), line.closed);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / references
// ---------------------------------------------------------------------------
TopoWeaveMod::TopoWeaveMod() {
    GetTopoWeaveDesc()->MakeAutoParamBlocks(this);
    session_.setSurface(&surface_);
}

TopoWeaveMod::~TopoWeaveMod() {
    delete palette_;
    palette_ = nullptr;
}

void TopoWeaveMod::SetReference(int i, RefTargetHandle rtarg) {
    if (i == PBLOCK_REF) pblock = static_cast<IParamBlock2*>(rtarg);
}

RefTargetHandle TopoWeaveMod::Clone(RemapDir& remap) {
    TopoWeaveMod* mod = new TopoWeaveMod();
    mod->ReplaceReference(PBLOCK_REF, remap.CloneRef(pblock));
    mod->mesh_ = mesh_;
    mod->meshInitialized_ = meshInitialized_;
    BaseClone(this, mod, remap);
    return mod;
}

RefResult TopoWeaveMod::NotifyRefChanged(const Interval& /*changeInt*/, RefTargetHandle hTarget,
                                         PartID& /*partID*/, RefMessage message, BOOL /*propagate*/) {
    if (hTarget == pblock && message == REFMSG_CHANGE) {
        const ParamID id = pblock->LastNotifyParamID();
        // pb_hp_nodes is also reported when a source node moves or deforms.
        if (id == pb_hp_nodes) surfaceDirty_ = true;
        if (id == pb_auto_v) {
            if (IParamMap2* map = pblock->GetMap()) map->Enable(pb_v_segments, !pblock->GetInt(pb_auto_v));
        }
        previewValid_ = false;
        if (ip_) {
            RequestRedraw(true);
            UpdatePalette();
        }
        // Parameters only drive tools and previews; the authored mesh (our
        // output) is unchanged, so the pipeline need not be re-evaluated.
        return REF_STOP;
    }
    return REF_SUCCEED;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
IOResult TopoWeaveMod::Save(ISave* isave) {
    Modifier::Save(isave);
    ULONG nb = 0;

    isave->BeginChunk(CHUNK_STATE);
    int init = meshInitialized_ ? 1 : 0;
    isave->Write(&init, sizeof(init), &nb);
    isave->EndChunk();

    isave->BeginChunk(CHUNK_MESH);
    int version = 1;
    isave->Write(&version, sizeof(version), &nb);
    int nv = mesh_.vertexCount();
    isave->Write(&nv, sizeof(nv), &nb);
    if (nv > 0) isave->Write(mesh_.verts.data(), static_cast<ULONG>(sizeof(rtp::Vec3) * nv), &nb);
    int nf = mesh_.faceCount();
    isave->Write(&nf, sizeof(nf), &nb);
    for (int f = 0; f < nf; ++f) {
        const auto& face = mesh_.faces[f];
        int deg = static_cast<int>(face.size());
        isave->Write(&deg, sizeof(deg), &nb);
        if (deg > 0) isave->Write(face.data(), static_cast<ULONG>(sizeof(int) * deg), &nb);
        char frozen = mesh_.isFrozen(f) ? 1 : 0;
        isave->Write(&frozen, sizeof(frozen), &nb);
    }
    isave->EndChunk();
    return IO_OK;
}

IOResult TopoWeaveMod::Load(ILoad* iload) {
    Modifier::Load(iload);
    IOResult res;
    ULONG nb = 0;
    while (IO_OK == (res = iload->OpenChunk())) {
        switch (iload->CurChunkID()) {
        case CHUNK_STATE: {
            int init = 0;
            res = iload->Read(&init, sizeof(init), &nb);
            meshInitialized_ = init != 0;
            break;
        }
        case CHUNK_MESH: {
            rtp::PolyMesh mesh;
            int version = 0, nv = 0, nf = 0;
            res = iload->Read(&version, sizeof(version), &nb);
            if (res == IO_OK) res = iload->Read(&nv, sizeof(nv), &nb);
            if (res == IO_OK && nv > 0) {
                mesh.verts.resize(nv);
                res = iload->Read(mesh.verts.data(), static_cast<ULONG>(sizeof(rtp::Vec3) * nv), &nb);
            }
            if (res == IO_OK) res = iload->Read(&nf, sizeof(nf), &nb);
            for (int f = 0; res == IO_OK && f < nf; ++f) {
                int deg = 0;
                res = iload->Read(&deg, sizeof(deg), &nb);
                std::vector<int> face(deg > 0 ? deg : 0);
                if (res == IO_OK && deg > 0)
                    res = iload->Read(face.data(), static_cast<ULONG>(sizeof(int) * deg), &nb);
                char frozen = 0;
                if (res == IO_OK) res = iload->Read(&frozen, sizeof(frozen), &nb);
                bool valid = deg >= 3;
                for (int v : face) valid = valid && v >= 0 && v < nv;
                if (valid) {
                    const int idx = mesh.addFace(std::move(face));
                    mesh.frozen[idx] = frozen;
                }
            }
            if (res == IO_OK) mesh_ = std::move(mesh);
            break;
        }
        default:
            break;
        }
        iload->CloseChunk();
        if (res != IO_OK) return res;
    }
    return IO_OK;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------
void TopoWeaveMod::ImportFrom(MNMesh& mm) {
    rtp::PolyMesh mesh;
    std::vector<int> remap(mm.numv, -1);
    for (int i = 0; i < mm.numv; ++i) {
        if (mm.V(i)->GetFlag(MN_DEAD)) continue;
        remap[i] = mesh.addVertex(ToVec(mm.V(i)->p));
    }
    for (int f = 0; f < mm.numf; ++f) {
        MNFace* face = mm.F(f);
        if (face->GetFlag(MN_DEAD) || face->deg < 3) continue;
        std::vector<int> idx(face->deg);
        bool valid = true;
        for (int k = 0; k < face->deg; ++k) {
            const int v = face->vtx[k];
            idx[k] = (v >= 0 && v < mm.numv) ? remap[v] : -1;
            valid = valid && idx[k] >= 0;
        }
        if (valid) mesh.addFace(std::move(idx));
    }
    mesh_ = std::move(mesh);
}

void TopoWeaveMod::WriteTo(MNMesh& mm) const {
    mm.ClearAndFree();
    const int nv = mesh_.vertexCount();
    mm.setNumVerts(nv);
    for (int i = 0; i < nv; ++i) mm.V(i)->p = ToPoint(mesh_.verts[i]);

    int nf = 0;
    for (const auto& face : mesh_.faces)
        if (face.size() >= 3) ++nf;
    mm.setNumFaces(nf);
    int f = 0;
    std::vector<int> buf;
    for (const auto& face : mesh_.faces) {
        if (face.size() < 3) continue;
        buf.assign(face.begin(), face.end());
        mm.F(f)->MakePoly(static_cast<int>(buf.size()), buf.data());
        mm.F(f)->smGroup = 1;
        ++f;
    }
    mm.selLevel = MNM_SL_OBJECT;
    mm.InvalidateGeomCache();
    mm.InvalidateTopoCache();
    mm.FillInMesh();
}

void TopoWeaveMod::ModifyObject(TimeValue /*t*/, ModContext& /*mc*/, ObjectState* os, INode* /*node*/) {
    if (!os->obj->IsSubClassOf(polyObjectClassID)) return;
    PolyObject* poly = static_cast<PolyObject*>(os->obj);
    MNMesh& mm = poly->GetMesh();
    if (!meshInitialized_ || reimportRequested_) {
        // The first evaluation adopts the input topology (an empty Editable
        // Poly, a plane, or an existing partial retopology to continue).
        ImportFrom(mm);
        meshInitialized_ = true;
        reimportRequested_ = false;
        previewValid_ = false;
    }
    WriteTo(mm);
    const Interval valid = FOREVER;
    poly->UpdateValidity(GEOM_CHAN_NUM, valid);
    poly->UpdateValidity(TOPO_CHAN_NUM, valid);
    poly->UpdateValidity(SELECT_CHAN_NUM, valid);
    poly->UpdateValidity(TEXMAP_CHAN_NUM, valid);
    poly->UpdateValidity(VERT_COLOR_CHAN_NUM, valid);
}

// ---------------------------------------------------------------------------
// Spaces and the High Poly surface
// ---------------------------------------------------------------------------
INode* TopoWeaveMod::EditNode() {
    if (!ip_) return nullptr;
    ModContextList mcList;
    INodeTab nodes;
    ip_->GetModContexts(mcList, nodes);
    INode* node = nodes.Count() > 0 ? nodes[0]->GetActualINode() : nullptr;
    nodes.DisposeTemporary();
    return node;
}

Matrix3 TopoWeaveMod::ObjectTM(TimeValue t) {
    INode* node = EditNode();
    return node ? node->GetObjectTM(t) : Matrix3(1);
}

rtp::Ray TopoWeaveMod::WorldRayToObject(const Ray& r, TimeValue t) {
    const Matrix3 inv = Inverse(ObjectTM(t));
    const Point3 o = r.p * inv;
    const Point3 d = Normalize(VectorTransform(inv, r.dir));
    return rtp::Ray(ToVec(o), ToVec(d));
}

bool TopoWeaveMod::EnsureSurface(TimeValue t) {
    INode* self = EditNode();
    if (!self) return !surface_.empty();
    const Matrix3 selfTM = self->GetObjectTM(t);
    std::vector<INode*> nodes;
    for (int i = 0; i < pblock->Count(pb_hp_nodes); ++i) {
        INode* n = pblock->GetINode(pb_hp_nodes, t, i);
        if (n && n != self) nodes.push_back(n);
    }
    if (!surfaceDirty_ && nodes == surfaceNodes_ && selfTM == surfaceSelfTM_) return !surface_.empty();

    const Matrix3 inv = Inverse(selfTM);
    rtp::TriMesh merged;
    for (INode* n : nodes) {
        const ObjectState hos = n->EvalWorldState(t);
        if (!hos.obj || hos.obj->SuperClassID() != GEOMOBJECT_CLASS_ID) continue;
        if (!hos.obj->CanConvertToType(triObjectClassID)) continue;
        TriObject* tri = static_cast<TriObject*>(hos.obj->ConvertToType(t, triObjectClassID));
        if (!tri) continue;
        Mesh& m = tri->GetMesh();
        const Matrix3 tm = n->GetObjectTM(t) * inv;
        rtp::TriMesh part;
        part.verts.reserve(m.getNumVerts());
        for (int v = 0; v < m.getNumVerts(); ++v) part.verts.push_back(ToVec(m.getVert(v) * tm));
        part.tris.reserve(m.getNumFaces());
        for (int f = 0; f < m.getNumFaces(); ++f) {
            const Face& face = m.faces[f];
            part.tris.push_back({static_cast<int>(face.v[0]), static_cast<int>(face.v[1]),
                                 static_cast<int>(face.v[2])});
        }
        merged.append(part);
        if (tri != hos.obj) tri->MaybeAutoDelete();
    }
    surface_.build(std::move(merged));
    session_.setSurface(&surface_);
    surfaceNodes_ = nodes;
    surfaceSelfTM_ = selfTM;
    surfaceDirty_ = false;
    previewValid_ = false;
    return !surface_.empty();
}

// ---------------------------------------------------------------------------
// Editing UI lifetime
// ---------------------------------------------------------------------------
void TopoWeaveMod::BeginEditParams(IObjParam* ip, ULONG flags, Animatable* prev) {
    ip_ = ip;
    GetTopoWeaveDesc()->BeginEditParams(ip, this, flags, prev);
    for (int tool = TW_TOOL_BRIDGE; tool < TW_TOOL_COUNT; ++tool) modes_[tool] = new TWCommandMode(this, tool);
    TWActivateActions(this);
    surfaceDirty_ = true;
    previewValid_ = false;
    ShowPalette(true);
}

void TopoWeaveMod::EndEditParams(IObjParam* ip, ULONG flags, Animatable* next) {
    for (int tool = TW_TOOL_BRIDGE; tool < TW_TOOL_COUNT; ++tool) {
        if (!modes_[tool]) continue;
        ip->DeleteMode(modes_[tool]);
        delete modes_[tool];
        modes_[tool] = nullptr;
    }
    TWDeactivateActions();
    delete palette_;
    palette_ = nullptr;
    GetTopoWeaveDesc()->EndEditParams(ip, this, flags, next);
    feedback = Feedback();
    activeTool_ = TW_TOOL_NONE;
    ip_ = nullptr;
}

void TopoWeaveMod::SetTool(int tool) {
    if (!ip_) return;
    const bool ourModeOnTop = activeTool_ != TW_TOOL_NONE && ip_->GetCommandMode() == modes_[activeTool_];
    if (tool == TW_TOOL_NONE || tool == activeTool_) {
        if (ourModeOnTop) ip_->PopCommandMode();
        return;
    }
    if (tool <= TW_TOOL_NONE || tool >= TW_TOOL_COUNT || !modes_[tool]) return;
    if (!EnsureSurface(ip_->GetTime())) {
        SetStatus(L"Assign a High Poly source first (Pick / Add...)");
        return;
    }
    if (ourModeOnTop) ip_->PopCommandMode();
    ip_->PushCommandMode(modes_[tool]);
}

void TopoWeaveMod::OnToolEntered(int tool) {
    activeTool_ = tool;
    UpdatePalette();
    RequestRedraw(false);
}

void TopoWeaveMod::OnToolExited(int tool) {
    if (activeTool_ == tool) activeTool_ = TW_TOOL_NONE;
    UpdatePalette();
    RequestRedraw(false);
}

// ---------------------------------------------------------------------------
// Bridge session
// ---------------------------------------------------------------------------
void TopoWeaveMod::SyncSessionSettings() {
    rtp::BridgeSettings& s = session_.settings();
    s.uSegments = pblock->GetInt(pb_u_segments);
    s.autoV = pblock->GetInt(pb_auto_v) != 0;
    s.vSegments = pblock->GetInt(pb_v_segments);
    s.offset = Offset();
    s.relaxIterations = pblock->GetInt(pb_relax_iters);
    s.smoothFlow = pblock->GetInt(pb_smooth_flow) != 0;
}

const rtp::BridgePreview& TopoWeaveMod::Preview() {
    if (!previewValid_) {
        SyncSessionSettings();
        if (EnsureSurface(GetCOREInterface()->GetTime())) {
            preview_ = session_.build();
        } else {
            preview_ = rtp::BridgePreview();
            preview_.message = "Assign a High Poly source first";
        }
        previewValid_ = true;
        UpdatePalette();
    }
    return preview_;
}

void TopoWeaveMod::AddStroke(const rtp::Polyline& curve, int forcedRole) {
    if (forcedRole == 1 && session_.guides().size() < 2) {
        SetStatus(L"Rails need two guides first");
        return;
    }
    BeginSessionEdit();
    const wchar_t* what = L"guide";
    if (forcedRole == 0) {
        session_.addGuide(curve);
    } else if (forcedRole == 1) {
        session_.addRail(curve);
        what = L"rail";
    } else if (session_.addAuto(curve) == rtp::StrokeRole::Rail) {
        what = L"rail";
    }
    EndSessionEdit(_M("TopoWeave Stroke"));
    std::wstring msg = L"Added ";
    msg += what;
    if (curve.closed) msg += L" (closed)";
    msg += L"  -  ";
    msg += Widen(Preview().message);
    SetStatus(msg);
}

bool TopoWeaveMod::AddBorderGuide(const rtp::Polyline& stroke, double tolerance) {
    rtp::BorderChain chain;
    if (!rtp::pickBorderSpan(mesh_, stroke, tolerance, chain)) return false;
    BeginSessionEdit();
    session_.addBorderGuide(mesh_, chain);
    EndSessionEdit(_M("TopoWeave Border Guide"));
    SetStatus(L"Border loaded as guide  -  " + Widen(Preview().message));
    return true;
}

bool TopoWeaveMod::DeleteStrokeNear(const rtp::Vec3& p, double tolerance) {
    rtp::StrokeRole role;
    int index = -1;
    if (!session_.nearestStroke(p, tolerance, role, index)) return false;
    BeginSessionEdit();
    session_.removeStroke(role, index);
    EndSessionEdit(_M("TopoWeave Delete Stroke"));
    return true;
}

void TopoWeaveMod::RemoveLastStroke() {
    if (!session_.hasStrokes()) return;
    BeginSessionEdit();
    session_.removeLast();
    EndSessionEdit(_M("TopoWeave Remove Stroke"));
}

void TopoWeaveMod::ClearStrokes() {
    if (!session_.hasStrokes()) return;
    BeginSessionEdit();
    session_.clear();
    EndSessionEdit(_M("TopoWeave Clear Strokes"));
    SetStatus(L"Strokes cleared");
}

bool TopoWeaveMod::CommitBridge(bool /*keepToolActive*/) {
    const rtp::BridgePreview preview = Preview();
    if (!preview.ok) {
        SetStatus(Widen(preview.message));
        return false;
    }
    BeginMeshEdit();
    const bool weld = pblock->GetInt(pb_auto_weld) != 0;
    const double threshold = pblock->GetFloat(pb_weld_threshold);
    const bool cont = pblock->GetInt(pb_continue) != 0;
    const rtp::CommitResult r = session_.commit(mesh_, preview, weld, threshold, cont);
    if (!r.ok) {
        CancelMeshEdit();
        SetStatus(Widen(r.message));
        return false;
    }
    if (pblock->GetInt(pb_live_conform) && !surface_.empty())
        rtp::conformVertices(mesh_, surface_, Offset(), r.newVerts);
    meshInitialized_ = true;
    previewValid_ = false;
    EndMeshEdit(_M("TopoWeave Commit"));
    SetStatus(Widen(r.message));
    return true;
}

// ---------------------------------------------------------------------------
// Undo scopes
// ---------------------------------------------------------------------------
void TopoWeaveMod::BeginMeshEdit() {
    theHold.Begin();
    theHold.Put(new TWMeshRestore(this, true));
    holdOpen_ = true;
}

void TopoWeaveMod::EndMeshEdit(const MCHAR* undoName) {
    if (!holdOpen_) return;
    holdOpen_ = false;
    theHold.Accept(undoName);
    MeshChanged();
}

void TopoWeaveMod::BeginSessionEdit() {
    theHold.Begin();
    theHold.Put(new TWMeshRestore(this, false));
    holdOpen_ = true;
}

void TopoWeaveMod::EndSessionEdit(const MCHAR* undoName) {
    if (!holdOpen_) return;
    holdOpen_ = false;
    theHold.Accept(undoName);
    previewValid_ = false;
    RequestRedraw(false);
    UpdatePalette();
}

void TopoWeaveMod::BeginParamEdit() {
    theHold.Begin();
    holdOpen_ = true;
}

void TopoWeaveMod::EndParamEdit(const MCHAR* undoName) {
    if (!holdOpen_) return;
    holdOpen_ = false;
    theHold.Accept(undoName);
}

void TopoWeaveMod::CancelMeshEdit() {
    if (!holdOpen_) return;
    holdOpen_ = false;
    theHold.Cancel();  // restores every snapshot taken since Begin
    previewValid_ = false;
    RequestRedraw(false);
}

// ---------------------------------------------------------------------------
// Mesh level operations
// ---------------------------------------------------------------------------
void TopoWeaveMod::MeshChanged() {
    previewValid_ = false;
    NotifyDependents(FOREVER, PART_ALL, REFMSG_CHANGE);
    RequestRedraw(false);
}

void TopoWeaveMod::ConformAll() {
    if (!EnsureSurface(GetCOREInterface()->GetTime())) {
        SetStatus(L"Assign a High Poly source first");
        return;
    }
    BeginMeshEdit();
    const int n = rtp::conformVertices(mesh_, surface_, Offset());
    EndMeshEdit(_M("TopoWeave Conform All"));
    SetStatus(L"Conformed " + std::to_wstring(n) + L" vertices");
}

void TopoWeaveMod::RelaxAll() {
    const bool hasSurface = EnsureSurface(GetCOREInterface()->GetTime());
    BeginMeshEdit();
    rtp::RelaxOptions opt;
    opt.iterations = (std::max)(1, pblock->GetInt(pb_relax_iters) * 2);
    opt.strength = 0.5;
    opt.offset = Offset();
    opt.reproject = hasSurface;
    rtp::relaxVertices(mesh_, hasSurface ? &surface_ : nullptr, opt);
    EndMeshEdit(_M("TopoWeave Relax All"));
    SetStatus(L"Relaxed the whole mesh");
}

void TopoWeaveMod::ShowInspector(HWND parent) {
    const rtp::TopologyStats s = mesh_.inspect();
    std::wstring text = Widen(s.report());
    if (s.triangles + s.ngons > 0) text += L"\nTip: triangles and n-gons usually need attention before subdivision.";
    if (s.degenerateFaces + s.isolatedVertices > 0) text += L"\nUse Repair to remove degenerate faces and isolated vertices.";
    MessageBoxW(parent, text.c_str(), L"TopoWeave - Topology Inspector", MB_OK | MB_ICONINFORMATION);
}

void TopoWeaveMod::RepairMesh() {
    BeginMeshEdit();
    const int fixes = mesh_.repair();
    EndMeshEdit(_M("TopoWeave Repair"));
    SetStatus(L"Repair: " + std::to_wstring(fixes) + L" fixes");
}

void TopoWeaveMod::SnapToWorldZero(int axis) {
    const TimeValue t = GetCOREInterface()->GetTime();
    const Matrix3 tm = ObjectTM(t);
    const Matrix3 inv = Inverse(tm);
    const float threshold = pblock->GetFloat(pb_weld_threshold);
    const auto frozen = mesh_.frozenVertexMask();
    BeginMeshEdit();
    int n = 0;
    for (int v = 0; v < mesh_.vertexCount(); ++v) {
        if (frozen[v]) continue;
        Point3 w = ToPoint(mesh_.verts[v]) * tm;  // world space, honours object offset
        if (std::fabs(w[axis]) > threshold || w[axis] == 0.0f) continue;
        w[axis] = 0.0f;
        mesh_.verts[v] = ToVec(w * inv);
        ++n;
    }
    EndMeshEdit(_M("TopoWeave Snap To Zero"));
    const wchar_t* names[3] = {L"X", L"Y", L"Z"};
    SetStatus(L"Snapped " + std::to_wstring(n) + L" vertices to world " + names[axis] + L"=0");
}

void TopoWeaveMod::ResetFromInput() {
    BeginMeshEdit();
    reimportRequested_ = true;
    session_.clear();
    EndMeshEdit(_M("TopoWeave Reset From Input"));
}

void TopoWeaveMod::ClearMesh() {
    BeginMeshEdit();
    mesh_ = rtp::PolyMesh();
    meshInitialized_ = true;
    session_.clear();
    EndMeshEdit(_M("TopoWeave Clear Mesh"));
}

void TopoWeaveMod::RequestRedraw(bool interactive) {
    if (ip_) ip_->RedrawViews(ip_->GetTime(), interactive ? REDRAW_INTERACTIVE : REDRAW_NORMAL);
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
float TopoWeaveMod::Offset() { return pblock->GetFloat(pb_offset); }
float TopoWeaveMod::BrushRadius() { return pblock->GetFloat(pb_brush_radius); }
float TopoWeaveMod::BrushStrength() { return pblock->GetFloat(pb_brush_strength); }
void TopoWeaveMod::SetBrushRadius(float r) { pblock->SetValue(pb_brush_radius, 0, (std::max)(r, 1e-4f)); }
void TopoWeaveMod::SetBrushStrength(float s) { pblock->SetValue(pb_brush_strength, 0, s); }

// ---------------------------------------------------------------------------
// Palette glue
// ---------------------------------------------------------------------------
void TopoWeaveMod::ShowPalette(bool show) {
    if (!ip_) return;
    if (!palette_ && show) palette_ = new TWPalette(this);
    if (palette_) palette_->Show(show);
}

void TopoWeaveMod::TogglePalette() { ShowPalette(!(palette_ && palette_->Visible())); }

void TopoWeaveMod::UpdatePalette() {
    if (palette_) palette_->Update();
}

void TopoWeaveMod::SetStatus(const std::wstring& text) {
    status_ = text;
    UpdatePalette();
    if (ip_) ip_->ReplacePrompt(text.c_str());
}

// ---------------------------------------------------------------------------
// Viewport display
// ---------------------------------------------------------------------------
int TopoWeaveMod::Display(TimeValue t, INode* inode, ViewExp* vpt, int /*flags*/, ModContext* /*mc*/) {
    if (!ip_ || !vpt || !vpt->IsAlive() || !inode) return 0;
    GraphicsWindow* gw = vpt->getGW();
    if (!gw) return 0;
    const DWORD savedLimits = gw->getRndLimits();
    gw->setRndLimits(savedLimits & ~GW_ILLUM);
    gw->setTransform(inode->GetObjectTM(t));

    // Authored cage (useful under TurboSmooth with Show End Result).
    if (pblock->GetInt(pb_show_cage) && mesh_.faceCount() > 0) {
        const Color c = pblock->GetColor(pb_cage_color);
        gw->setColor(LINE_COLOR, c.r, c.g, c.b);
        gw->startSegments();
        for (const auto& face : mesh_.faces) {
            const size_t n = face.size();
            for (size_t i = 0; i < n; ++i) {
                const int a = face[i], b = face[(i + 1) % n];
                Point3 seg[2] = {ToPoint(mesh_.verts[a]), ToPoint(mesh_.verts[b])};
                gw->segment(seg, 1);
            }
        }
        gw->endSegments();
        gw->setColor(LINE_COLOR, c.r * 0.6f, c.g * 0.6f, c.b * 0.6f);
        gw->startMarkers();
        for (const auto& v : mesh_.verts) {
            Point3 p = ToPoint(v);
            gw->marker(&p, DOT_MRKR);
        }
        gw->endMarkers();
    }

    // Pending band preview.
    const rtp::BridgePreview& p = Preview();
    if (p.ok) {
        const rtp::BridgeGrid& g = p.grid;
        gw->setColor(LINE_COLOR, 1.0f, 0.78f, 0.25f);
        std::vector<Point3> line;
        for (int r = 0; r < g.rows; ++r) {
            line.clear();
            for (int c = 0; c < g.cols; ++c) line.push_back(ToPoint(g.at(r, c)));
            DrawLine3D(gw, line, g.closedU);
        }
        for (int c = 0; c < g.cols; ++c) {
            line.clear();
            for (int r = 0; r < g.rows; ++r) line.push_back(ToPoint(g.at(r, c)));
            DrawLine3D(gw, line, false);
        }
    }

    // Strokes: guides green, border guides amber, rails cyan.
    for (const auto& guide : session_.guides()) {
        if (guide.isBorder()) gw->setColor(LINE_COLOR, 1.0f, 0.75f, 0.1f);
        else gw->setColor(LINE_COLOR, 0.25f, 0.95f, 0.35f);
        DrawPolyline(gw, guide.curve);
    }
    gw->setColor(LINE_COLOR, 0.2f, 0.8f, 1.0f);
    for (const auto& rail : session_.rails()) DrawPolyline(gw, rail.curve);

    // Brush footprint.
    if (feedback.brushVisible && activeTool_ != TW_TOOL_BRIDGE && activeTool_ != TW_TOOL_NONE) {
        const rtp::Vec3 n = rtp::normalize(ToVec(feedback.brushNormal));
        const rtp::Vec3 e1 = rtp::anyPerpendicular(n);
        const rtp::Vec3 e2 = rtp::cross(n, e1);
        const rtp::Vec3 c = ToVec(feedback.brushCenter);
        std::vector<Point3> circle;
        for (int i = 0; i < 48; ++i) {
            const double a = 2.0 * rtp::kPi * i / 48.0;
            circle.push_back(ToPoint(c + (e1 * std::cos(a) + e2 * std::sin(a)) * feedback.brushRadius));
        }
        gw->setColor(LINE_COLOR, 1.0f, 0.9f, 0.3f);
        DrawLine3D(gw, std::move(circle), true);
    }

    // Screen space feedback in the viewport the user is working in.
    if (vpt->GetHWnd() == feedback.hintWnd) {
        if (feedback.stroke.size() > 1) {
            if (feedback.strokeIsRail) gw->setColor(LINE_COLOR, 0.2f, 0.8f, 1.0f);
            else gw->setColor(LINE_COLOR, 1.0f, 1.0f, 1.0f);
            DrawLineScreen(gw, feedback.stroke);
        }
        if (!feedback.hint.empty() && activeTool_ != TW_TOOL_NONE) {
            gw->setColor(TEXT_COLOR, 1.0f, 1.0f, 0.85f);
            IPoint3 tp(feedback.hintPos.x, feedback.hintPos.y, 0);
            gw->wText(&tp, feedback.hint.c_str());
        }
    }

    gw->setRndLimits(savedLimits);
    return 0;
}

void TopoWeaveMod::GetWorldBoundBox(TimeValue t, INode* inode, ViewExp* /*vpt*/, Box3& box, ModContext* /*mc*/) {
    box.Init();
    if (!inode) return;
    const Matrix3 tm = inode->GetObjectTM(t);
    for (const auto& v : mesh_.verts) box += ToPoint(v) * tm;
    for (const auto& g : session_.guides())
        for (const auto& v : g.curve.pts) box += ToPoint(v) * tm;
    for (const auto& r : session_.rails())
        for (const auto& v : r.curve.pts) box += ToPoint(v) * tm;
    if (previewValid_ && preview_.ok)
        for (const auto& v : preview_.grid.pts) box += ToPoint(v) * tm;
    if (feedback.brushVisible) {
        const Point3 c = feedback.brushCenter * tm;
        const float r = feedback.brushRadius;
        box += c + Point3(r, r, r);
        box += c - Point3(r, r, r);
    }
}
