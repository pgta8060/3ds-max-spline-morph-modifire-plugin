// TopoWeave for 3ds Max - floating tool palette.
//
// The palette belongs to the TopoWeave modifier currently being edited: it is
// created in BeginEditParams and destroyed in EndEditParams, so it hides by
// itself whenever another object, base object or modifier becomes current.
#pragma once

#include "TopoWeaveMod.h"

class TWPalette {
public:
    explicit TWPalette(TopoWeaveMod* mod);
    ~TWPalette();

    void Show(bool show);
    bool Visible() const;
    void Update();

private:
    static INT_PTR CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnCommand(int id);
    void ShowGestures();

    TopoWeaveMod* mod_;
    HWND hwnd_ = nullptr;

    static bool s_hasPos;
    static POINT s_pos;
};
