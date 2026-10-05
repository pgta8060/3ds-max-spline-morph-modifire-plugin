// TopoWeave for 3ds Max - DLL entry points.
#include "TopoWeaveMod.h"

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

const MCHAR* GetString(int id) {
    // A small ring of buffers so a few strings can be alive at the same time.
    static MCHAR buffers[8][512];
    static int next = 0;
    if (!hInstance) return _M("");
    MCHAR* buf = buffers[next];
    next = (next + 1) % 8;
    return LoadString(hInstance, id, buf, 512) ? buf : _M("");
}

// Exported through TopoWeave.def.
extern "C" {

const MCHAR* LibDescription() { return GetString(IDS_LIBDESCRIPTION); }

int LibNumberClasses() { return 1; }

ClassDesc* LibClassDesc(int i) { return i == 0 ? GetTopoWeaveDesc() : nullptr; }

ULONG LibVersion() { return VERSION_3DSMAX; }

int LibInitialize() { return TRUE; }

int LibShutdown() { return TRUE; }

// The action table must be registered at startup for hotkeys to be listed.
ULONG CanAutoDefer() { return 0; }

}  // extern "C"
