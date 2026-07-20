#include "kisak_script_entity_android.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>

namespace {

// Same double-quoted "key" "value" scan as ParseModelEntities/ParseSpawnPoint
// (kisak_world_scene_android.cpp), generalized to capture every key instead
// of a fixed subset — this module needs classname plus the fields_1-derived
// set below, not just the props-rendering subset those functions cared about.
size_t NextQuoted(const std::string& text, size_t from, size_t end, std::string& value) {
    const size_t open = text.find('"', from);
    if (open == std::string::npos || open >= end) return std::string::npos;
    const size_t close = text.find('"', open + 1);
    if (close == std::string::npos || close > end) return std::string::npos;
    value.assign(text, open + 1, close - open - 1);
    return close + 1;
}

void ParseVec3(const std::string& text, float out[3]) {
    std::istringstream in(text);
    in >> out[0] >> out[1] >> out[2];
}

// script_model (g_scr_mover.cpp:199, via SP_script_model): needs a resolvable
// model + origin to be meaningful — matches the practical filter
// ParseModelEntities already applies for the same reason (an entity with no
// model can't be placed as a mesh), so the two paths stay comparable.
bool BuildScriptModel(const std::map<std::string, std::string>& fields, KisakScriptEntity& out) {
    auto modelIt = fields.find("model");
    auto originIt = fields.find("origin");
    if (modelIt == fields.end() || modelIt->second.empty()) return false;
    if (originIt == fields.end() || originIt->second.empty()) return false;
    out.model = modelIt->second;
    ParseVec3(originIt->second, out.origin);
    out.handler = KisakEntityHandler::ScriptModel;
    return true;
}

// trigger_multiple (g_trigger.cpp:111, via InitTrigger -> SV_SetBrushModel):
// real retail requires a brush "model" ref (e.g. "*3") to have real
// collision geometry, but this step only proves entity DATA exists in
// memory (explicit scope cut in the plan's objective) — no brush/collision
// integration here, so origin is enough to record a placeholder instance.
bool BuildTriggerMultiple(const std::map<std::string, std::string>& fields, KisakScriptEntity& out) {
    auto originIt = fields.find("origin");
    if (originIt == fields.end() || originIt->second.empty()) return false;
    ParseVec3(originIt->second, out.origin);
    auto modelIt = fields.find("model");
    if (modelIt != fields.end()) out.model = modelIt->second;
    out.handler = KisakEntityHandler::TriggerMultiple;
    return true;
}

}  // namespace

std::vector<KisakScriptEntity> SpawnEntitiesFromMapEntsString(const std::string& entities) {
    std::vector<KisakScriptEntity> out;
    size_t cursor = 0;
    while (true) {
        const size_t open = entities.find('{', cursor);
        if (open == std::string::npos) break;
        const size_t close = entities.find('}', open);
        if (close == std::string::npos) break;
        cursor = close + 1;

        std::map<std::string, std::string> fields;
        size_t at = open;
        while (at < close) {
            std::string key;
            std::string value;
            at = NextQuoted(entities, at, close, key);
            if (at == std::string::npos) break;
            at = NextQuoted(entities, at, close, value);
            if (at == std::string::npos) break;
            fields[key] = std::move(value);
        }

        auto classnameIt = fields.find("classname");
        if (classnameIt == fields.end()) continue;
        const std::string& classname = classnameIt->second;

        KisakScriptEntity ent;
        ent.classname = classname;

        bool built = false;
        if (classname == "script_model") {
            built = BuildScriptModel(fields, ent);
        } else if (classname == "trigger_multiple") {
            built = BuildTriggerMultiple(fields, ent);
        } else {
            // G_CallSpawn's own behavior for an unmatched classname: log and
            // skip, not an error (g_spawn.cpp:1019-1023) — see also the
            // header comment on why misc_model is deliberately NOT handled
            // here despite ParseModelEntities scanning for it.
            continue;
        }
        if (!built) continue;

        if (auto it = fields.find("angles"); it != fields.end()) ParseVec3(it->second, ent.angles);
        if (auto it = fields.find("target"); it != fields.end()) ent.target = it->second;
        if (auto it = fields.find("targetname"); it != fields.end()) ent.targetname = it->second;
        if (auto it = fields.find("script_linkname"); it != fields.end()) ent.scriptLinkName = it->second;
        if (auto it = fields.find("script_noteworthy"); it != fields.end()) ent.scriptNoteworthy = it->second;
        if (auto it = fields.find("spawnflags"); it != fields.end()) ent.spawnflags = std::atoi(it->second.c_str());

        out.push_back(std::move(ent));
    }
    return out;
}

std::string DescribeScriptEntities(const std::vector<KisakScriptEntity>& entities) {
    uint32_t scriptModelCount = 0;
    uint32_t triggerCount = 0;
    for (const auto& ent : entities) {
        if (ent.handler == KisakEntityHandler::ScriptModel) ++scriptModelCount;
        else if (ent.handler == KisakEntityHandler::TriggerMultiple) ++triggerCount;
    }
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "%zu entites (script_model=%u, trigger_multiple=%u)",
                  entities.size(), scriptModelCount, triggerCount);
    return buf;
}
