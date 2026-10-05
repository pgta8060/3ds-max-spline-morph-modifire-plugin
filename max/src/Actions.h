// TopoWeave for 3ds Max - hotkey action table.
//
// The table is only active while a TopoWeave modifier is the current entry of
// the modify panel, so the number keys go back to 3ds Max (or to whatever is
// selected) as soon as you leave TopoWeave. Keys can be remapped in
// Customize > Hotkey Editor under the "TopoWeave" group.
#pragma once

#include <actiontable.h>

class TopoWeaveMod;

ActionTable* TWBuildActionTable();
void TWActivateActions(TopoWeaveMod* mod);
void TWDeactivateActions();
