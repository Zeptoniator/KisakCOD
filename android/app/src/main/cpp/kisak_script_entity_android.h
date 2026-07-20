#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Minimal entity model + a small classname-dispatch spawn path — blueprint
// plans/android-gscript-vm-port.md, step 5. Standalone and NOT wired into
// BuildWorldScene/ParseModelEntities (kisak_world_scene_android.cpp) — that
// integration decision belongs to step 9. This module exists to prove a
// script-VM-friendly spawn path can produce the same entity data
// ParseModelEntities already does, from the exact same raw map_ents text.
//
// Reference: G_SpawnEntitiesFromString -> G_ParseSpawnVars -> SP_worldspawn
// -> G_CallSpawn (src/game/g_spawn.cpp:963,1206) dispatching classname to a
// spawn function via s_bspOrDynamicSpawns/s_bspOnlySpawns (g_spawn.cpp:45,71).
//
// gentity_s field layout — grep-CONFIRMED per the plan's explicit
// instruction, not inherited from the plan's own (admittedly incomplete)
// summary: the struct is NOT in g_local.h/g_shared.h (g_shared.h doesn't
// exist in this tree) — it's src/bgame/bg_public.h:918 (the KISAK_SP
// #elif branch; the #if branch at line 679 is the MP struct). Relevant
// fields: classname/target/targetname/script_linkName/script_noteworthy
// are uint16_t string-table ids (not char*); origin/angles live at
// s.lerp.pos.trBase[3] / s.lerp.apos.trBase[3] (LerpEntityState, via
// trajectory_t, src/qcommon/ent.h:174 + src/universal/q_shared.h:828);
// handler is an EntHandler_t (bg_public.h:805). The generic spawn-var ->
// field mapping used by G_ParseEntityFields is fields_1 (g_spawn.cpp:18):
// classname/origin/model/spawnflags/target/targetname/count/health/dmg/
// angles/script_linkname/script_noteworthy/maxhealth/anglelerprate/
// activator — this struct keeps only the subset script_model and
// trigger_multiple actually read (count/health/dmg/maxhealth are
// actor/item-only, out of scope here).
//
// misc_model is NOT a real classname anywhere in g_spawn.cpp's spawn
// tables — confirmed by grep across the whole file. ParseModelEntities
// (kisak_world_scene_android.cpp) scans for it defensively alongside
// script_model, but nothing in G_CallSpawn's real dispatch would ever
// route it anywhere; this module intentionally does NOT special-case it,
// since doing so would be un-faithful to the reference for no benefit —
// see the step 5 findings note in plans/gscript-real-source-notes.md.

enum class KisakEntityHandler : uint8_t {
    None = 0,
    ScriptModel,      // ENT_HANDLER_SCRIPT_MODEL (SP_script_model, g_scr_mover.cpp:199)
    TriggerMultiple,  // ENT_HANDLER_TRIGGER_MULTIPLE (SP_trigger_multiple, g_trigger.cpp:111)
};

struct KisakScriptEntity {
    std::string classname;
    std::string model;          // xmodel name (script_model) or brush ref e.g. "*3" (trigger_multiple)
    std::string target;
    std::string targetname;
    std::string scriptLinkName;      // script_linkname
    std::string scriptNoteworthy;    // script_noteworthy
    float origin[3] = {0.0f, 0.0f, 0.0f};
    float angles[3] = {0.0f, 0.0f, 0.0f};  // pitch, yaw, roll
    int32_t spawnflags = 0;
    KisakEntityHandler handler = KisakEntityHandler::None;
};

// Parses the SAME raw map_ents text block format ParseModelEntities/
// ParseSpawnPoint already scan ({ "key" "value" ... } blocks, double-quoted,
// no escaping) and dispatches each block's classname through a small
// classname -> handler table covering exactly script_model and
// trigger_multiple — not the full retail spawn table. Unrecognized
// classnames are skipped, matching G_CallSpawn's own "doesn't have a spawn
// function" no-op path (g_spawn.cpp:1019-1023) rather than erroring.
std::vector<KisakScriptEntity> SpawnEntitiesFromMapEntsString(const std::string& entities);

std::string DescribeScriptEntities(const std::vector<KisakScriptEntity>& entities);
