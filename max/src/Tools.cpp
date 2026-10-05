#include "Tools.h"

#include "Restore.h"

#include <cmath>

namespace {

std::wstring Widen(const char* s) {
    std::wstring w;
    for (; s && *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

double ScreenLength(const std::vector<IPoint2>& pts) {
    double len = 0;
    for (size_t i = 1; i < pts.size(); ++i)
        len += std::hypot(double(pts[i].x - pts[i - 1].x), double(pts[i].y - pts[i - 1].y));
    return len;
}

constexpr double kPickPixels = 12.0;

}  // namespace

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool TWMouseProc::screenRay(HWND hwnd, IPoint2 m, rtp::Ray& objRay) {
    ViewExp& vpt = GetCOREInterface()->GetViewExp(hwnd);
    if (!vpt.IsAlive()) return false;
    Ray r;
    vpt.MapScreenToWorldRay(static_cast<float>(m.x), static_cast<float>(m.y), r);
    objRay = mod_->WorldRayToObject(r, GetCOREInterface()->GetTime());
    return true;
}

bool TWMouseProc::surfaceHit(HWND hwnd, IPoint2 m, rtp::SurfaceHit& hit) {
    rtp::Ray r;
    return mod_->HasSurface() && screenRay(hwnd, m, r) && mod_->Surface().raycast(r, hit);
}

double TWMouseProc::pixelSize(HWND hwnd, IPoint2 m, double depth) {
    rtp::Ray a, b;
    if (!screenRay(hwnd, m, a) || !screenRay(hwnd, IPoint2(m.x + 10, m.y), b)) return 0.0;
    return rtp::distance(a.at(depth), b.at(depth)) / 10.0;
}

rtp::Vec3 TWMouseProc::viewDirection(HWND hwnd) {
    ViewExp& vpt = GetCOREInterface()->GetViewExp(hwnd);
    if (!vpt.IsAlive() || !vpt.getGW()) return rtp::Vec3();
    GraphicsWindow* gw = vpt.getGW();
    rtp::Ray r;
    if (!screenRay(hwnd, IPoint2(gw->getWinSizeX() / 2, gw->getWinSizeY() / 2), r)) return rtp::Vec3();
    return r.dir;
}

Point3 TWMouseProc::overlayPoint(const rtp::Ray& r, double frontFraction) {
    rtp::SurfaceHit h;
    if (mod_->HasSurface() && mod_->Surface().raycast(r, h)) {
        overlayDepth_ = h.t;
    } else if (overlayDepth_ <= 0.0) {
        const rtp::Aabb& b = mod_->Surface().bounds();
        overlayDepth_ = b.valid() ? (std::max)(rtp::dot(b.center() - r.origin, r.dir), 1e-3) : 100.0;
    }
    return ToPoint(r.at(overlayDepth_ * frontFraction));
}

TWMouseProc::Gesture TWMouseProc::classify(int flags) const {
    const bool c = (flags & MOUSE_CTRL) != 0;
    const bool s = (flags & MOUSE_SHIFT) != 0;
    const bool a = (flags & MOUSE_ALT) != 0;
    if (isBrushTool()) {
        if (c && s && a) return G_BRUSH_RADIUS;
        if (c && a) return G_BRUSH_STRENGTH;
        return G_BRUSH;
    }
    if (c && s && a) return G_DELETE;
    if (c && s) return G_BORDER;
    if (c && a) return G_ADJUST_V;
    if (c) return G_ADJUST_U;
    if (s && a) return G_TWIST;
    if (a) return G_CONTOUR;
    if (s) return G_RAIL;
    return G_STROKE;
}

// ---------------------------------------------------------------------------
// Mouse dispatch
// ---------------------------------------------------------------------------
int TWMouseProc::proc(HWND hwnd, int msg, int point, int flags, IPoint2 m) {
    switch (msg) {
    case MOUSE_POINT:
        if (point == 0) {
            begin(hwnd, flags, m);
            return TRUE;
        }
        end(hwnd, m);
        return FALSE;
    case MOUSE_MOVE:
        move(hwnd, flags, m);
        return TRUE;
    case MOUSE_DBLCLICK:
        if (!isBrushTool()) mod_->CommitBridge(true);
        return FALSE;
    case MOUSE_PROPCLICK:
        // Right click: commit the pending band and keep drawing.
        if (!isBrushTool() && mod_->Preview().ok) mod_->CommitBridge(true);
        return FALSE;
    case MOUSE_FREEMOVE:
        hover(hwnd, flags, m);
        return TRUE;
    case MOUSE_ABORT:
        Cancel();
        return FALSE;
    default:
        break;
    }
    return TRUE;
}

void TWMouseProc::Cancel() {
    switch (gesture_) {
    case G_BRUSH:
    case G_ADJUST_U:
    case G_ADJUST_V:
    case G_TWIST:
    case G_BRUSH_RADIUS:
    case G_BRUSH_STRENGTH:
        mod_->CancelMeshEdit();
        break;
    default:
        break;
    }
    gesture_ = G_NONE;
    screen_.clear();
    rays_.clear();
    mod_->feedback.stroke.clear();
    mod_->RequestRedraw(false);
}

void TWMouseProc::begin(HWND hwnd, int flags, IPoint2 m) {
    gesture_ = classify(flags);
    start_ = m;
    screen_.assign(1, m);
    rays_.clear();
    touched_.clear();
    hasPrevHit_ = false;

    const TimeValue t = GetCOREInterface()->GetTime();
    const bool needsSurface = gesture_ != G_ADJUST_U && gesture_ != G_ADJUST_V && gesture_ != G_TWIST &&
                              gesture_ != G_BRUSH_RADIUS && gesture_ != G_BRUSH_STRENGTH;
    if (!mod_->EnsureSurface(t) && needsSurface) {
        mod_->SetStatus(L"Assign a High Poly source first (Pick / Add...)");
        gesture_ = G_NONE;
        return;
    }
    rtp::Ray r;
    auto& fb = mod_->feedback;
    fb.stroke.clear();
    if (screenRay(hwnd, m, r)) {
        rays_.push_back(r);
        fb.stroke.push_back(overlayPoint(r));
    }
    fb.strokeIsRail = gesture_ == G_RAIL;
    fb.hintWnd = hwnd;

    IParamBlock2* pb = mod_->pblock;
    switch (gesture_) {
    case G_ADJUST_U:
        mod_->BeginParamEdit();
        startInt_ = pb->GetInt(pb_u_segments);
        break;
    case G_ADJUST_V: {
        mod_->BeginParamEdit();
        const auto& p = mod_->Preview();
        startInt_ = (p.ok && !p.vSegments.empty()) ? p.vSegments.back() : pb->GetInt(pb_v_segments);
        pb->SetValue(pb_auto_v, 0, FALSE);
        pb->SetValue(pb_v_segments, 0, startInt_);
        break;
    }
    case G_TWIST: {
        twistGuide_ = -1;
        int bestId = -1;
        const auto& gs = mod_->Session().guides();
        for (int i = 0; i < static_cast<int>(gs.size()); ++i)
            if (gs[i].curve.closed && gs[i].id > bestId) {
                bestId = gs[i].id;
                twistGuide_ = i;
            }
        if (twistGuide_ < 0) {
            mod_->SetStatus(L"Twist needs a closed ring (draw contours with Alt)");
            gesture_ = G_NONE;
            return;
        }
        startDouble_ = gs[twistGuide_].twist;
        mod_->BeginSessionEdit();
        break;
    }
    case G_BRUSH: {
        mod_->BeginMeshEdit();
        rtp::SurfaceHit h;
        if (surfaceHit(hwnd, m, h)) applyBrush(h);
        break;
    }
    case G_BRUSH_RADIUS:
        mod_->BeginParamEdit();
        startDouble_ = mod_->BrushRadius();
        break;
    case G_BRUSH_STRENGTH:
        mod_->BeginParamEdit();
        startDouble_ = mod_->BrushStrength();
        break;
    default:
        break;
    }
    updateHint(hwnd, flags, m);
}

void TWMouseProc::move(HWND hwnd, int flags, IPoint2 m) {
    const int dx = m.x - start_.x;
    IParamBlock2* pb = mod_->pblock;
    switch (gesture_) {
    case G_STROKE:
    case G_RAIL:
    case G_CONTOUR:
    case G_BORDER:
    case G_DELETE: {
        const IPoint2 last = screen_.back();
        if (std::abs(m.x - last.x) + std::abs(m.y - last.y) < 2) break;
        screen_.push_back(m);
        rtp::Ray r;
        if (screenRay(hwnd, m, r)) {
            rays_.push_back(r);
            mod_->feedback.stroke.push_back(overlayPoint(r));
        }
        mod_->RequestRedraw(true);
        break;
    }
    case G_ADJUST_U: {
        const int value = (std::max)(1, startInt_ + dx / 12);
        if (value != pb->GetInt(pb_u_segments)) pb->SetValue(pb_u_segments, 0, value);
        break;
    }
    case G_ADJUST_V: {
        const int value = (std::max)(1, startInt_ + dx / 12);
        if (value != pb->GetInt(pb_v_segments)) pb->SetValue(pb_v_segments, 0, value);
        break;
    }
    case G_TWIST:
        mod_->Session().setTwist(twistGuide_, startDouble_ + dx / 600.0);
        mod_->InvalidatePreview();
        mod_->RequestRedraw(true);
        break;
    case G_BRUSH: {
        rtp::SurfaceHit h;
        if (surfaceHit(hwnd, m, h)) applyBrush(h);
        break;
    }
    case G_BRUSH_RADIUS:
        mod_->SetBrushRadius(static_cast<float>(startDouble_ * std::exp(dx / 150.0)));
        mod_->feedback.brushRadius = mod_->BrushRadius();
        mod_->RequestRedraw(true);
        break;
    case G_BRUSH_STRENGTH:
        mod_->SetBrushStrength(static_cast<float>(rtp::clamp(startDouble_ + dx / 300.0, 0.01, 1.0)));
        break;
    default:
        break;
    }
    updateHint(hwnd, flags, m);
}

void TWMouseProc::end(HWND hwnd, IPoint2 m) {
    const Gesture g = gesture_;
    gesture_ = G_NONE;
    mod_->feedback.stroke.clear();
    const double screenLen = ScreenLength(screen_);

    switch (g) {
    case G_STROKE:
    case G_RAIL: {
        if (rays_.size() < 2 || screenLen < 4) break;
        rtp::StrokeOptions opt;
        opt.offset = mod_->Offset();
        // Closure is decided in screen space: what the user sees is what counts.
        const double gap = std::hypot(double(screen_.back().x - screen_.front().x),
                                      double(screen_.back().y - screen_.front().y));
        opt.allowClose = false;
        opt.forceClosed = gap < 15.0 && screenLen > 80.0;
        const rtp::StrokeResult res = rtp::strokeFromRays(rays_, mod_->Surface(), opt);
        if (!res.ok) {
            mod_->SetStatus(Widen(res.message));
            break;
        }
        mod_->AddStroke(res.curve, g == G_RAIL ? 1 : -1);
        break;
    }
    case G_CONTOUR: {
        if (rays_.size() < 2) break;
        rtp::ContourOptions opt;
        opt.offset = mod_->Offset();
        opt.viewDir = viewDirection(hwnd);
        const rtp::StrokeResult res = rtp::contourFromRays(rays_, mod_->Surface(), opt);
        if (!res.ok) {
            mod_->SetStatus(Widen(res.message));
            break;
        }
        mod_->AddStroke(res.curve, 0);
        break;
    }
    case G_BORDER: {
        rtp::SurfaceHit h;
        if (!surfaceHit(hwnd, screen_.front(), h)) {
            mod_->SetStatus(L"Start on the High Poly, over an open border");
            break;
        }
        const double tol = (std::max)(pixelSize(hwnd, screen_.front(), h.t) * kPickPixels, 1e-6);
        rtp::Polyline stroke;
        if (screenLen < 6 || rays_.size() < 2) {
            stroke.pts.push_back(h.point);  // click: whole loop
        } else {
            rtp::StrokeOptions opt;
            opt.allowClose = false;
            const rtp::StrokeResult res = rtp::strokeFromRays(rays_, mod_->Surface(), opt);
            if (res.ok) stroke = res.curve;
            else stroke.pts.push_back(h.point);
        }
        if (!mod_->AddBorderGuide(stroke, tol)) mod_->SetStatus(L"No open border under the stroke");
        break;
    }
    case G_DELETE: {
        rtp::SurfaceHit h;
        if (surfaceHit(hwnd, m, h)) {
            const double tol = pixelSize(hwnd, m, h.t) * kPickPixels;
            if (!mod_->DeleteStrokeNear(h.point, tol)) mod_->SetStatus(L"No stroke under the cursor");
        }
        break;
    }
    case G_ADJUST_U:
    case G_ADJUST_V:
    case G_BRUSH_RADIUS:
    case G_BRUSH_STRENGTH:
        mod_->EndParamEdit(_M("TopoWeave Adjust"));
        break;
    case G_TWIST:
        mod_->EndSessionEdit(_M("TopoWeave Twist"));
        break;
    case G_BRUSH: {
        if (tool_ == TW_TOOL_MOVE && !touched_.empty() && mod_->pblock->GetInt(pb_auto_weld)) {
            const double threshold = mod_->pblock->GetFloat(pb_weld_threshold);
            rtp::autoWeld(mod_->Authored(), touched_, threshold);
        }
        static const MCHAR* names[TW_TOOL_COUNT] = {_M(""), _M(""), _M("TopoWeave Relax"),
                                                    _M("TopoWeave Move"), _M("TopoWeave Conform")};
        mod_->EndMeshEdit(names[tool_]);
        break;
    }
    default:
        break;
    }
    screen_.clear();
    rays_.clear();
    touched_.clear();
    mod_->RequestRedraw(false);
    mod_->UpdatePalette();
}

void TWMouseProc::applyBrush(const rtp::SurfaceHit& hit) {
    rtp::PolyMesh& mesh = mod_->Authored();
    const rtp::Surface& S = mod_->Surface();
    const double off = mod_->Offset();
    const double radius = mod_->BrushRadius();
    const double strength = mod_->BrushStrength();
    switch (tool_) {
    case TW_TOOL_RELAX:
        rtp::relaxBrush(mesh, S, off, hit.point, radius, strength);
        break;
    case TW_TOOL_CONFORM:
        rtp::conformBrush(mesh, S, off, hit.point, radius);
        break;
    case TW_TOOL_MOVE:
        if (hasPrevHit_) {
            for (const auto& s : rtp::brushFootprint(mesh, prevHit_, radius)) touched_.push_back(s.vertex);
            rtp::moveBrush(mesh, S, off, prevHit_, radius, hit.point - prevHit_, strength);
        }
        break;
    default:
        break;
    }
    prevHit_ = hit.point;
    hasPrevHit_ = true;
    auto& fb = mod_->feedback;
    fb.brushVisible = true;
    fb.brushCenter = ToPoint(hit.point);
    fb.brushNormal = ToPoint(hit.normal);
    fb.brushRadius = static_cast<float>(radius);
    mod_->MeshChanged();
}

void TWMouseProc::hover(HWND hwnd, int flags, IPoint2 m) {
    SetCursor(LoadCursor(nullptr, IDC_CROSS));
    auto& fb = mod_->feedback;
    fb.hintWnd = hwnd;
    if (isBrushTool()) {
        rtp::SurfaceHit h;
        fb.brushVisible = mod_->EnsureSurface(GetCOREInterface()->GetTime()) && surfaceHit(hwnd, m, h);
        if (fb.brushVisible) {
            fb.brushCenter = ToPoint(h.point);
            fb.brushNormal = ToPoint(h.normal);
            fb.brushRadius = mod_->BrushRadius();
        }
    }
    updateHint(hwnd, flags, m);
    mod_->RequestRedraw(true);
}

void TWMouseProc::updateHint(HWND hwnd, int flags, IPoint2 m) {
    const bool c = (flags & MOUSE_CTRL) != 0;
    const bool s = (flags & MOUSE_SHIFT) != 0;
    const bool a = (flags & MOUSE_ALT) != 0;
    wchar_t buf[256];
    std::wstring hint;
    if (isBrushTool()) {
        const wchar_t* name = tool_ == TW_TOOL_RELAX ? L"Relax" : tool_ == TW_TOOL_MOVE ? L"Move" : L"Conform";
        if (c && s && a) hint = L"Drag: brush radius";
        else if (c && a) hint = L"Drag: brush strength";
        else {
            swprintf(buf, 256, L"%ls brush  r=%.3g  s=%.2f", name, mod_->BrushRadius(), mod_->BrushStrength());
            hint = buf;
        }
    } else if (c && s && a) {
        hint = L"Click a stroke to delete it";
    } else if (c && s) {
        hint = L"Drag along an open border (click = whole loop)";
    } else if (c && a) {
        hint = L"Drag left/right: rows per span";
    } else if (c) {
        swprintf(buf, 256, L"Drag left/right: segments (%d)", mod_->pblock->GetInt(pb_u_segments));
        hint = buf;
    } else if (s && a) {
        hint = L"Drag left/right: twist the last ring";
    } else if (a) {
        hint = L"Drag across a limb: contour ring";
    } else if (s) {
        hint = L"Draw a rail across the guides";
    } else {
        const auto& gs = mod_->Session().guides();
        if (gs.empty()) hint = L"Draw the first guide";
        else if (gs.size() == 1) hint = L"Draw the next guide";
        else {
            const auto& p = mod_->Preview();
            hint = Widen(p.message.c_str());
            if (p.ok) hint += L"   [dbl-click / Enter / RMB: commit]";
        }
    }
    auto& fb = mod_->feedback;
    fb.hint = hint;
    fb.hintWnd = hwnd;
    rtp::Ray hr;
    fb.hintAnchorValid = screenRay(hwnd, IPoint2(m.x + 18, m.y + 22), hr);
    if (fb.hintAnchorValid) fb.hintAnchor = overlayPoint(hr, 0.5);  // well in front of everything
    if (IObjParam* ip = mod_->Interface()) ip->ReplacePrompt(hint.c_str());
}

// ---------------------------------------------------------------------------
// Command mode
// ---------------------------------------------------------------------------
void TWCommandMode::EnterMode() {
    SetCursor(LoadCursor(nullptr, IDC_CROSS));
    if (IObjParam* ip = mod_->Interface()) ip->PushPrompt(_M("TopoWeave"));
    mod_->OnToolEntered(tool_);
}

void TWCommandMode::ExitMode() {
    // A right click exits command modes in 3ds Max. If that is why we are
    // leaving, honour the "RMB commits the pending band" convention.
    if (tool_ == TW_TOOL_BRIDGE && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) && mod_->Preview().ok)
        mod_->CommitBridge(false);
    proc_.Cancel();
    mod_->feedback.brushVisible = false;
    mod_->feedback.hint.clear();
    if (IObjParam* ip = mod_->Interface()) ip->PopPrompt();
    mod_->OnToolExited(tool_);
}
