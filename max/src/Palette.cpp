#include "Palette.h"

bool TWPalette::s_hasPos = false;
POINT TWPalette::s_pos = {0, 0};

namespace {
const wchar_t* kGestures =
    L"SPLINE BRIDGE  (1)\n"
    L"  LMB drag                Draw a guide; a stroke crossing two guides becomes a rail\n"
    L"  Shift + LMB drag        Draw a rail (flow line across the guides)\n"
    L"  Alt + LMB drag          Contour: drag across a limb to get a full ring\n"
    L"  Ctrl + LMB drag         Segments along the guides\n"
    L"  Ctrl + Alt + LMB drag   Rows per span\n"
    L"  Shift + Alt + LMB drag  Twist the last ring\n"
    L"  Ctrl + Shift + LMB      Use an open border (drag = span, click = whole loop)\n"
    L"  Ctrl + Shift + Alt LMB  Delete the stroke under the cursor\n"
    L"  Double click / Enter    Commit and keep drawing\n"
    L"  RMB                     Commit (exits when nothing is pending)\n"
    L"  Ctrl + Z                Undo the last stroke or commit\n"
    L"\n"
    L"More guides = more control over the band's shape.\n"
    L"More rails  = more control over the flow inside the band.\n"
    L"\n"
    L"BRUSHES  (2 Relax, 3 Move, 4 Conform)\n"
    L"  LMB drag                Apply the brush\n"
    L"  Ctrl + Alt + LMB drag   Brush strength\n"
    L"  Ctrl + Shift + Alt drag Brush radius\n";
}  // namespace

TWPalette::TWPalette(TopoWeaveMod* mod) : mod_(mod) {
    IObjParam* ip = mod_->Interface();
    hwnd_ = CreateDialogParam(hInstance, MAKEINTRESOURCE(IDD_PALETTE), ip ? ip->GetMAXHWnd() : nullptr, Proc,
                              reinterpret_cast<LPARAM>(this));
    if (hwnd_ && ip) ip->RegisterDlgWnd(hwnd_);
}

TWPalette::~TWPalette() {
    if (!hwnd_) return;
    RECT rc;
    if (GetWindowRect(hwnd_, &rc)) {
        s_pos = {rc.left, rc.top};
        s_hasPos = true;
    }
    if (IObjParam* ip = mod_->Interface()) ip->UnRegisterDlgWnd(hwnd_);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void TWPalette::Show(bool show) {
    if (!hwnd_) return;
    ShowWindow(hwnd_, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (show) Update();
}

bool TWPalette::Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }

void TWPalette::Update() {
    if (!hwnd_) return;
    const int tool = mod_->ActiveTool();
    CheckDlgButton(hwnd_, IDC_PAL_BRIDGE, tool == TW_TOOL_BRIDGE ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_PAL_RELAX, tool == TW_TOOL_RELAX ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_PAL_MOVE, tool == TW_TOOL_MOVE ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, IDC_PAL_CONFORM, tool == TW_TOOL_CONFORM ? BST_CHECKED : BST_UNCHECKED);

    const bool strokes = mod_->Session().hasStrokes();
    EnableWindow(GetDlgItem(hwnd_, IDC_PAL_CANCEL), strokes);
    EnableWindow(GetDlgItem(hwnd_, IDC_PAL_UNDO_STROKE), strokes);

    std::wstring text = mod_->Status();
    const size_t guides = mod_->Session().guides().size();
    const size_t rails = mod_->Session().rails().size();
    wchar_t buf[96];
    swprintf(buf, 96, L"\nGuides: %zu   Rails: %zu", guides, rails);
    text += buf;
    SetDlgItemTextW(hwnd_, IDC_PAL_STATUS, text.c_str());
}

void TWPalette::OnCommand(int id) {
    switch (id) {
    case IDC_PAL_BRIDGE: mod_->SetTool(TW_TOOL_BRIDGE); break;
    case IDC_PAL_RELAX: mod_->SetTool(TW_TOOL_RELAX); break;
    case IDC_PAL_MOVE: mod_->SetTool(TW_TOOL_MOVE); break;
    case IDC_PAL_CONFORM: mod_->SetTool(TW_TOOL_CONFORM); break;
    case IDC_PAL_COMMIT: mod_->CommitBridge(true); break;
    case IDC_PAL_CANCEL: mod_->ClearStrokes(); break;
    case IDC_PAL_UNDO_STROKE: mod_->RemoveLastStroke(); break;
    case IDC_PAL_HELP: ShowGestures(); break;
    default: break;
    }
    Update();
}

void TWPalette::ShowGestures() {
    MessageBoxW(hwnd_, kGestures, L"TopoWeave - Gestures", MB_OK | MB_ICONINFORMATION);
}

INT_PTR CALLBACK TWPalette::Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TWPalette* self = reinterpret_cast<TWPalette*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);
        self = reinterpret_cast<TWPalette*>(lParam);
        if (s_hasPos) {
            SetWindowPos(hwnd, nullptr, s_pos.x, s_pos.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else if (IObjParam* ip = self->mod_->Interface()) {
            // First time: dock next to the top left corner of the Max window.
            RECT rc;
            GetWindowRect(ip->GetMAXHWnd(), &rc);
            SetWindowPos(hwnd, nullptr, rc.left + 80, rc.top + 140, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return TRUE;
    }
    case WM_COMMAND:
        if (self && HIWORD(wParam) == BN_CLICKED) self->OnCommand(LOWORD(wParam));
        return TRUE;
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return TRUE;
    case WM_MOVE: {
        RECT rc;
        if (GetWindowRect(hwnd, &rc)) {
            s_pos = {rc.left, rc.top};
            s_hasPos = true;
        }
        return FALSE;
    }
    default:
        return FALSE;
    }
}
