// TopoWeave for 3ds Max - class descriptor, parameter block and rollout logic.
//
// The class descriptor and the parameter block descriptor live in the same
// translation unit on purpose: ParamBlockDesc2 registers itself with the class
// descriptor during static initialisation, which is only well ordered inside
// one file.
#include "Actions.h"
#include "TopoWeaveMod.h"

#include <hold.h>

// ---------------------------------------------------------------------------
// Class descriptor
// ---------------------------------------------------------------------------
class TopoWeaveClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    void* Create(BOOL /*loading*/ = FALSE) override { return new TopoWeaveMod(); }
    const MCHAR* ClassName() override { return GetString(IDS_CLASS_NAME); }
#if TW_MAX2022
    const MCHAR* NonLocalizedClassName() override { return _M("TopoWeave"); }
#endif
    SClass_ID SuperClassID() override { return OSM_CLASS_ID; }
    Class_ID ClassID() override { return TOPOWEAVE_CLASS_ID; }
    const MCHAR* Category() override { return GetString(IDS_CATEGORY); }
    const MCHAR* InternalName() override { return _M("TopoWeave"); }
    HINSTANCE HInstance() override { return hInstance; }
    int NumActionTables() override { return 1; }
    ActionTable* GetActionTable(int /*i*/) override { return TWBuildActionTable(); }
};

static TopoWeaveClassDesc s_desc;
ClassDesc2* GetTopoWeaveDesc() { return &s_desc; }

// ---------------------------------------------------------------------------
// High Poly validation: geometry only, never the retopology node itself.
// ---------------------------------------------------------------------------
namespace {

bool IsValidSource(INode* node, ReferenceMaker* owner) {
    if (!node) return false;
    const ObjectState os = node->EvalWorldState(GetCOREInterface()->GetTime());
    if (!os.obj || os.obj->SuperClassID() != GEOMOBJECT_CLASS_ID) return false;
    if (auto* mod = dynamic_cast<TopoWeaveMod*>(owner)) {
        if (node == mod->EditNode()) return false;
        // Reject duplicates.
        IParamBlock2* pb = mod->pblock;
        for (int i = 0; i < pb->Count(pb_hp_nodes); ++i)
            if (pb->GetINode(pb_hp_nodes, 0, i) == node) return false;
    }
    return true;
}

class HPValidator : public PBValidator {
public:
    BOOL Validate(PB2Value& v) override { return IsValidSource(static_cast<INode*>(v.r), nullptr); }
    BOOL Validate(PB2Value& v, ReferenceMaker* owner, ParamID /*id*/, int /*tabIndex*/) override {
        return IsValidSource(static_cast<INode*>(v.r), owner);
    }
};
HPValidator s_hpValidator;

// "Add..." button: pick several High Poly sources by name.
class HPPickByName : public HitByNameDlgCallback {
public:
    explicit HPPickByName(TopoWeaveMod* mod) : mod_(mod) {}
    const MCHAR* dialogTitle() override { return _M("Add High Poly Sources"); }
    const MCHAR* buttonText() override { return _M("Add"); }
    BOOL singleSelect() override { return FALSE; }
    BOOL useFilter() override { return TRUE; }
    int filter(INode* node) override { return IsValidSource(node, mod_) ? TRUE : FALSE; }
    BOOL useProc() override { return TRUE; }
    void proc(INodeTab& nodeTab) override {
        if (nodeTab.Count() == 0) return;
        theHold.Begin();
        for (int i = 0; i < nodeTab.Count(); ++i) {
            INode* node = nodeTab[i];
            if (IsValidSource(node, mod_)) mod_->pblock->Append(pb_hp_nodes, 1, &node);
        }
        theHold.Accept(_M("Add High Poly Sources"));
    }

private:
    TopoWeaveMod* mod_;
};

// ---------------------------------------------------------------------------
// Rollout dialog procedure (buttons that are not parameters).
// ---------------------------------------------------------------------------
class TWParamDlgProc : public ParamMap2UserDlgProc {
public:
    INT_PTR DlgProc(TimeValue /*t*/, IParamMap2* map, HWND hWnd, UINT msg, WPARAM wParam,
                    LPARAM /*lParam*/) override {
        IParamBlock2* pb = map->GetParamBlock();
        TopoWeaveMod* mod = pb ? static_cast<TopoWeaveMod*>(pb->GetOwner()) : nullptr;
        if (!mod) return FALSE;
        switch (msg) {
        case WM_INITDIALOG: {
            SetDlgItemText(hWnd, IDC_VERSION, _M("TopoWeave 0.1.0"));
            map->Enable(pb_v_segments, !pb->GetInt(pb_auto_v));
            return TRUE;
        }
        case WM_COMMAND:
            if (HIWORD(wParam) != BN_CLICKED) break;
            switch (LOWORD(wParam)) {
            case IDC_HP_ADD: {
                HPPickByName cb(mod);
                if (IObjParam* ip = mod->Interface()) ip->DoHitByNameDialog(&cb);
                return TRUE;
            }
            case IDC_HP_CLEAR:
                theHold.Begin();
                pb->ZeroCount(pb_hp_nodes);
                theHold.Accept(_M("Clear High Poly Sources"));
                return TRUE;
            case IDC_LIVE_CONFORM:
                if (IsDlgButtonChecked(hWnd, IDC_LIVE_CONFORM) == BST_CHECKED) mod->ConformAll();
                return TRUE;
            case IDC_OPEN_PALETTE: mod->TogglePalette(); return TRUE;
            case IDC_CONFORM_ALL: mod->ConformAll(); return TRUE;
            case IDC_RELAX_ALL: mod->RelaxAll(); return TRUE;
            case IDC_INSPECT: mod->ShowInspector(hWnd); return TRUE;
            case IDC_REPAIR: mod->RepairMesh(); return TRUE;
            case IDC_SNAP_X: mod->SnapToWorldZero(0); return TRUE;
            case IDC_SNAP_Y: mod->SnapToWorldZero(1); return TRUE;
            case IDC_SNAP_Z: mod->SnapToWorldZero(2); return TRUE;
            case IDC_RESET_INPUT:
                if (MessageBoxW(hWnd, L"Replace the authored topology with the modifier input?",
                                L"TopoWeave", MB_YESNO | MB_ICONQUESTION) == IDYES)
                    mod->ResetFromInput();
                return TRUE;
            case IDC_CLEAR_MESH:
                if (MessageBoxW(hWnd, L"Delete all authored topology? (Undo is available)", L"TopoWeave",
                                MB_YESNO | MB_ICONQUESTION) == IDYES)
                    mod->ClearMesh();
                return TRUE;
            default:
                break;
            }
            break;
        default:
            break;
        }
        return FALSE;
    }
    void DeleteThis() override {}
};
TWParamDlgProc s_dlgProc;

}  // namespace

// ---------------------------------------------------------------------------
// Parameter block
// ---------------------------------------------------------------------------
static ParamBlockDesc2 s_paramBlock(
    tw_params, _M("params"), 0, &s_desc, P_AUTO_CONSTRUCT + P_AUTO_UI, TopoWeaveMod::PBLOCK_REF,
    // rollout
    IDD_PARAMS, IDS_PARAMS, 0, 0, &s_dlgProc,
    // High Poly sources
    pb_hp_nodes, _M("highPolySources"), TYPE_INODE_TAB, 0, P_VARIABLE_SIZE, IDS_HP_NODES,
        p_ui, TYPE_NODELISTBOX, IDC_HP_LIST, IDC_HP_PICK, 0, IDC_HP_REMOVE,
        p_validator, &s_hpValidator,
        p_end,
    pb_offset, _M("offset"), TYPE_WORLD, 0, IDS_OFFSET,
        p_default, 0.05f,
        p_range, 0.0f, 1.0e6f,
        p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_OFFSET_EDIT, IDC_OFFSET_SPIN, SPIN_AUTOSCALE,
        p_end,
    pb_live_conform, _M("liveConform"), TYPE_BOOL, 0, IDS_LIVE_CONFORM,
        p_default, FALSE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_LIVE_CONFORM,
        p_end,
    pb_auto_weld, _M("autoWeld"), TYPE_BOOL, 0, IDS_AUTO_WELD,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_AUTO_WELD,
        p_end,
    pb_weld_threshold, _M("weldThreshold"), TYPE_WORLD, 0, IDS_WELD_THRESHOLD,
        p_default, 0.1f,
        p_range, 0.0f, 1.0e6f,
        p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_WELD_EDIT, IDC_WELD_SPIN, SPIN_AUTOSCALE,
        p_end,
    pb_show_cage, _M("showCage"), TYPE_BOOL, 0, IDS_SHOW_CAGE,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_SHOW_CAGE,
        p_end,
    pb_cage_color, _M("cageColor"), TYPE_RGBA, 0, IDS_CAGE_COLOR,
        p_default, Color(1.0f, 0.55f, 0.1f),
        p_ui, TYPE_COLORSWATCH, IDC_CAGE_COLOR,
        p_end,
    pb_u_segments, _M("segments"), TYPE_INT, 0, IDS_U_SEGMENTS,
        p_default, 12,
        p_range, 1, 1024,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_U_EDIT, IDC_U_SPIN, 1.0f,
        p_end,
    pb_auto_v, _M("autoRows"), TYPE_BOOL, 0, IDS_AUTO_V,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_AUTO_V,
        p_end,
    pb_v_segments, _M("rowsPerSpan"), TYPE_INT, 0, IDS_V_SEGMENTS,
        p_default, 4,
        p_range, 1, 1024,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_V_EDIT, IDC_V_SPIN, 1.0f,
        p_end,
    pb_relax_iters, _M("relaxIterations"), TYPE_INT, 0, IDS_RELAX_ITERS,
        p_default, 2,
        p_range, 0, 50,
        p_ui, TYPE_SPINNER, EDITTYPE_INT, IDC_RELAX_EDIT, IDC_RELAX_SPIN, 1.0f,
        p_end,
    pb_continue, _M("continueAfterCommit"), TYPE_BOOL, 0, IDS_CONTINUE,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_CONTINUE,
        p_end,
    pb_brush_radius, _M("brushRadius"), TYPE_WORLD, 0, IDS_BRUSH_RADIUS,
        p_default, 5.0f,
        p_range, 0.0001f, 1.0e6f,
        p_ui, TYPE_SPINNER, EDITTYPE_UNIVERSE, IDC_BRUSH_R_EDIT, IDC_BRUSH_R_SPIN, SPIN_AUTOSCALE,
        p_end,
    pb_brush_strength, _M("brushStrength"), TYPE_FLOAT, 0, IDS_BRUSH_STRENGTH,
        p_default, 0.5f,
        p_range, 0.01f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_BRUSH_S_EDIT, IDC_BRUSH_S_SPIN, 0.01f,
        p_end,
    pb_smooth_flow, _M("smoothFlow"), TYPE_BOOL, 0, IDS_SMOOTH_FLOW,
        p_default, TRUE,
        p_ui, TYPE_SINGLECHEKBOX, IDC_SMOOTH_FLOW,
        p_end,
    p_end);
