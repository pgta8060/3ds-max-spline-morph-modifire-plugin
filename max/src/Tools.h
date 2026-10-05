// TopoWeave for 3ds Max - viewport tools (command modes + mouse procs).
#pragma once

#include "TopoWeaveMod.h"

#include <cmdmode.h>
#include <mouseman.h>

class TWMouseProc : public MouseCallBack {
public:
    TWMouseProc(TopoWeaveMod* mod, int tool) : mod_(mod), tool_(tool) {}

    int proc(HWND hwnd, int msg, int point, int flags, IPoint2 m) override;
    // Always use press-drag-release semantics, whatever the user preference.
    int override(int /*mode*/) override { return CLICK_DRAG_CLICK; }

    void Cancel();

private:
    enum Gesture {
        G_NONE,
        G_STROKE,        // LMB drag: guide or rail (auto)
        G_RAIL,          // Shift: force rail
        G_CONTOUR,       // Alt: contour ring across a limb
        G_BORDER,        // Ctrl+Shift: use an open border (span or click = loop)
        G_ADJUST_U,      // Ctrl: segments along the guides
        G_ADJUST_V,      // Ctrl+Alt: rows per span
        G_TWIST,         // Shift+Alt: twist the last closed guide
        G_DELETE,        // Ctrl+Shift+Alt click: delete stroke under cursor
        G_BRUSH,         // brush tools: paint
        G_BRUSH_RADIUS,  // Ctrl+Shift+Alt drag
        G_BRUSH_STRENGTH // Ctrl+Alt drag
    };

    bool isBrushTool() const { return tool_ != TW_TOOL_BRIDGE; }
    Gesture classify(int flags) const;
    void begin(HWND hwnd, int flags, IPoint2 m);
    void move(HWND hwnd, int flags, IPoint2 m);
    void end(HWND hwnd, IPoint2 m);
    void hover(HWND hwnd, int flags, IPoint2 m);

    bool screenRay(HWND hwnd, IPoint2 m, rtp::Ray& objRay);
    bool surfaceHit(HWND hwnd, IPoint2 m, rtp::SurfaceHit& hit);
    double pixelSize(HWND hwnd, IPoint2 m, double depth);
    rtp::Vec3 viewDirection(HWND hwnd);
    void applyBrush(const rtp::SurfaceHit& hit);
    void updateHint(HWND hwnd, int flags, IPoint2 m);

    TopoWeaveMod* mod_;
    int tool_;
    Gesture gesture_ = G_NONE;
    IPoint2 start_;
    std::vector<IPoint2> screen_;
    std::vector<rtp::Ray> rays_;
    int startInt_ = 0;
    double startDouble_ = 0;
    int twistGuide_ = -1;
    bool hasPrevHit_ = false;
    rtp::Vec3 prevHit_;
    std::vector<int> touched_;
};

class TWCommandMode : public CommandMode {
public:
    TWCommandMode(TopoWeaveMod* mod, int tool) : mod_(mod), tool_(tool), proc_(mod, tool) {}

    int Class() override { return MODIFY_COMMAND; }
    int ID() override { return CID_USER + 0x7A10 + tool_; }
    MouseCallBack* MouseProc(int* numPoints) override {
        *numPoints = 2;  // press + release
        return &proc_;
    }
    ChangeForegroundCallback* ChangeFGProc() override { return CHANGE_FG_SELECTED; }
    BOOL ChangeFG(CommandMode* oldMode) override { return oldMode->ChangeFGProc() != CHANGE_FG_SELECTED; }
    void EnterMode() override;
    void ExitMode() override;

    int Tool() const { return tool_; }

private:
    TopoWeaveMod* mod_;
    int tool_;
    TWMouseProc proc_;
};
