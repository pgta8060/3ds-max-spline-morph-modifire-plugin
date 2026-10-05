#include "Actions.h"

#include "TopoWeaveMod.h"

namespace {

const ActionTableId kTWActions = 0x7a3b10c2;
const ActionContextId kTWContext = 0x7a3b10c3;

ActionDescription s_actions[] = {
    {ID_TW_BRIDGE, IDS_ACT_BRIDGE, IDS_ACT_BRIDGE, IDS_ACTIONS},
    {ID_TW_RELAX, IDS_ACT_RELAX, IDS_ACT_RELAX, IDS_ACTIONS},
    {ID_TW_MOVE, IDS_ACT_MOVE, IDS_ACT_MOVE, IDS_ACTIONS},
    {ID_TW_CONFORM, IDS_ACT_CONFORM, IDS_ACT_CONFORM, IDS_ACTIONS},
    {ID_TW_COMMIT, IDS_ACT_COMMIT, IDS_ACT_COMMIT, IDS_ACTIONS},
    {ID_TW_CANCEL, IDS_ACT_CANCEL, IDS_ACT_CANCEL, IDS_ACTIONS},
    {ID_TW_PALETTE, IDS_ACT_PALETTE, IDS_ACT_PALETTE, IDS_ACTIONS},
};

class TWActionCallback : public ActionCallback {
public:
    TopoWeaveMod* mod = nullptr;

    BOOL ExecuteAction(int id) override {
        if (!mod) return FALSE;
        switch (id) {
        case ID_TW_BRIDGE: mod->SetTool(TW_TOOL_BRIDGE); return TRUE;
        case ID_TW_RELAX: mod->SetTool(TW_TOOL_RELAX); return TRUE;
        case ID_TW_MOVE: mod->SetTool(TW_TOOL_MOVE); return TRUE;
        case ID_TW_CONFORM: mod->SetTool(TW_TOOL_CONFORM); return TRUE;
        case ID_TW_COMMIT: mod->CommitBridge(true); return TRUE;
        case ID_TW_CANCEL: mod->ClearStrokes(); return TRUE;
        case ID_TW_PALETTE: mod->TogglePalette(); return TRUE;
        default: return FALSE;
        }
    }
};

TWActionCallback s_callback;
bool s_active = false;

}  // namespace

ActionTable* TWBuildActionTable() {
    MSTR name = GetString(IDS_ACTIONS);
    HACCEL accel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATORS));
    const int count = static_cast<int>(sizeof(s_actions) / sizeof(s_actions[0]));
    ActionTable* table = new ActionTable(kTWActions, kTWContext, name, accel, count, s_actions, hInstance);
    GetCOREInterface()->GetActionManager()->RegisterActionContext(kTWContext, name.data());
    return table;
}

void TWActivateActions(TopoWeaveMod* mod) {
    s_callback.mod = mod;
    IActionManager* am = GetCOREInterface()->GetActionManager();
    if (am && !s_active) s_active = am->ActivateActionTable(&s_callback, kTWActions) != FALSE;
}

void TWDeactivateActions() {
    IActionManager* am = GetCOREInterface()->GetActionManager();
    if (am && s_active) am->DeactivateActionTable(&s_callback, kTWActions);
    s_active = false;
    s_callback.mod = nullptr;
}
