// TopoWeave for 3ds Max - native undo / redo.
//
// One restore object snapshots both the authored mesh and the stroke session,
// so committing a band, painting a brush stroke, adding a guide or twisting a
// ring are all undone with the standard 3ds Max Undo (Ctrl+Z).
#pragma once

#include "TopoWeaveMod.h"

#include <hold.h>

class TWMeshRestore : public RestoreObj {
public:
    TWMeshRestore(TopoWeaveMod* mod, bool meshAffected)
        : mod_(mod),
          meshAffected_(meshAffected),
          undoMesh_(meshAffected ? mod->mesh_ : rtp::PolyMesh()),
          undoInit_(mod->meshInitialized_),
          undoState_(mod->session_.state()) {}

    void Restore(int isUndo) override {
        if (isUndo && !redoSaved_) {
            if (meshAffected_) redoMesh_ = mod_->mesh_;
            redoInit_ = mod_->meshInitialized_;
            redoState_ = mod_->session_.state();
            redoSaved_ = true;
        }
        apply(undoMesh_, undoInit_, undoState_);
    }

    void Redo() override {
        if (redoSaved_) apply(redoMesh_, redoInit_, redoState_);
    }

    int Size() override {
        const size_t verts = undoMesh_.verts.size() + redoMesh_.verts.size();
        size_t faces = 0;
        for (const auto& f : undoMesh_.faces) faces += f.size();
        for (const auto& f : redoMesh_.faces) faces += f.size();
        return static_cast<int>(sizeof(*this) + verts * sizeof(rtp::Vec3) + faces * sizeof(int));
    }

    MSTR Description() override { return MSTR(_M("TopoWeave edit")); }

private:
    void apply(const rtp::PolyMesh& mesh, bool init, const rtp::BridgeSession::State& state) {
        if (meshAffected_) {
            mod_->mesh_ = mesh;
            mod_->meshInitialized_ = init;
        }
        mod_->session_.restore(state);
        mod_->InvalidatePreview();
        if (meshAffected_) mod_->MeshChanged();
        else mod_->RequestRedraw(false);
        mod_->UpdatePalette();
    }

    TopoWeaveMod* mod_;
    bool meshAffected_;
    rtp::PolyMesh undoMesh_, redoMesh_;
    bool undoInit_, redoInit_ = false;
    rtp::BridgeSession::State undoState_, redoState_;
    bool redoSaved_ = false;
};
