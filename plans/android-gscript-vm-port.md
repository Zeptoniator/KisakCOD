# Blueprint: Port COD4's GScript VM to the KisakCOD Android port

**Status:** draft
**Created:** 2026-07-20
**Repo:** `/home/jacques/Projects/KisakCOD/` — fork remote `fork` → `github.com/Zeptoniator/KisakCOD.git` (writable), `origin` → `github.com/SwagSoftware/KisakCOD.git` (upstream, **read-only** — a PR from this line of work was already closed there; do not target `origin` with any PR from this plan)
**Base branch for all steps:** `android-port-bootstrap` (NOT `main`/`master` — this is the long-lived integration branch for the whole Android port; every step branches from and PRs back into it)
**Mode:** git + gh CLI available → full branch/PR workflow, direct-to-`fork` (see remote note above)

## Objective

Run a **real SP mission's init script** on the Android port: load `maps/<mapname>.gsc` from a real zone, compile it with a ported lexer/parser/compiler, execute its `main()` on a ported bytecode VM, and have that script (not a hardcoded scanner) drive at least basic entity spawning from `map_ents`. Full AI (`src/game/actor_*.cpp`, ~26,700 lines) is explicitly **out of scope** — target only the script VM (lexer/parser/compiler/interpreter, `src/script/`, ~46,200 lines) plus the minimal slice of the entity/spawn system (`src/game/g_spawn.cpp`) needed to spawn a `script_model`/trigger from script-driven code instead of the current hand-rolled string scan.

**Explicit scope cut (called out here, not left silent):** this plan proves "script → entity data exists in memory," validated by counts/logs — it does **not** wire script-spawned entities into the visible XModel-instancing render path `ParseModelEntities` already feeds. Making script-spawned entities actually render is real but separate follow-up work once this pipeline is validated; don't let a step quietly expand to cover it.

## Why this is a blueprint and not a single PR

This was scoped down from an in-session attempt this same day — direct investigation confirmed the VM alone is ~1,700 lines (self-contained-ish) but the compiler front end (lexer + yacc-generated parser + AST + emitter) is 6 files and 10,000+ lines, and genuinely compiles `.gsc` **text** fresh on every level load (COD4 ships no precompiled-bytecode cache). That's multiple weeks of focused work even before entity integration. Every step below is sized to be independently committable, testable, and mergeable without the later steps existing yet.

## What already exists (read before starting ANY step)

The Android port (`android/app/src/main/cpp/`) has, as of commit `6e00a89` on `android-port-bootstrap`:

- **Zone/fastfile loader** (`kisak_zone_loader_android.{h,cpp}`) — the established architecture pattern. 64-bit-clean, but preserves the retail 32-bit serialized block layout: pointer slots are `(block<<28|offset)+1`-encoded, not native pointers, resolved via `KisakZoneView`. **Every new subsystem below must follow this exact pattern** — do not invent a different pointer/ref convention.
- **Rawfile extraction** (`kisak_zone_rawfile_android.{h,cpp}`) — `ScanZoneRawFiles()`/`DumpZoneRawFiles()` already recover plain-text embedded files (configs, `.cfg`, etc.) from a zone by signature scan, not by filename/extension. `.gsc` source text is very likely captured by this same mechanism (COD4's `Load_RawFile` path) — **Step 1 confirms this**, don't assume it works for `.gsc` specifically until verified.
- **Menu dvar store** (`kisak_menu_expression_android.h` — **not** `kisak_menu_state_android.h`, verify with `grep -n KisakExprValue android/app/src/main/cpp/*.h` before trusting either) — `SetKisakUiDvar`/`GetKisakUiDvar` and `KisakExprValue` (a tagged int/float/string value type, `MakeInt`/`MakeFloat`/`MakeString`) already exist for the UI expression evaluator. Reuse `KisakExprValue` as GScript's runtime value representation if its tagged-union shape fits — it currently has **no entity-ref variant**, so weigh that gap explicitly in Step 3 rather than assuming a clean fit.
- **World scene / entities** (`kisak_world_scene_android.{h,cpp}`) — `ParseModelEntities()` currently hand-scans the `map_ents` entity string for `script_model`/`misc_model` blocks (`{ "key" "value" }` format) and spawns them as static instanced XModels directly, bypassing any script system. This is what Step 8/9 eventually augment — **do not break this working path**; script-driven spawning should be additive.
- **Renderer/gameplay loop** (`kisak_android_native_renderer.cpp`) — `FireWorldWeapon()`, `UpdateWorldCamera()`, `DrawWorldScene()`. No entity update loop exists yet (world is static once loaded + player camera). This plan does not add one — `main()` execution proves the VM runs, it does not need to run every frame.
- **Run/deploy workflow**: `.claude/skills/run-kisakcod-android/driver.sh` (build/install/launch/logs/crashcheck) and its `SKILL.md` — use this, don't re-invent adb incantations. Known gotcha documented there: `adb shell input tap` doesn't register in the world view (needs `input swipe X Y X Y 100` instead); a **separate, newly confirmed** gotcha this session: zero-distance swipes (`X Y X Y <duration>`) behave as an instant tap **regardless of duration** — for a genuinely HELD gesture, use non-zero start/end coordinates with the swipe backgrounded (`&`) and a screencap fired mid-gesture.
- **Device**: Xiaomi `23124RA7EO` (`a38b2d7c`), Adreno 610. COD4 retail data present at `/sdcard/Android/data/com.kisakcod.android/files/cod4/`. Screen auto-locks during long sessions — `adb shell input keyevent KEYCODE_WAKEUP` + swipe-up-to-unlock before any device step if touches silently stop registering (checked via `dumpsys window | grep mCurrentFocus` and a control screenshot, not assumed).

## Architecture facts locked in by research (2026-07-20)

Source: `/home/jacques/Projects/KisakCOD/src/script/` (31 files) + `src/game/g_scr_main.cpp`, `src/game/g_spawn.cpp`. All line numbers below are from the decompiled reference tree, not the not-yet-written Android port.

1. **No bytecode cache.** `Scr_LoadScriptInternal` (`scr_main.cpp:218-286`) appends `.gsc`, reads the file as a plain-text rawfile-equivalent, then always runs `ScriptParse` → `ScriptCompile` fresh. Loaded-script dedup is by filename hash, not a cached artifact.
2. **Opcode set**: `enum Opcode_t` at `scr_vm.h:16-157`, **139 enum entries = 138 real opcodes + the `OP_count=0x8A` sentinel**. The enum's underlying type is `__int32`, but the on-wire opcode byte in the bytecode stream is a single unsigned byte (`*(unsigned char*)fs.pos++`) — don't conflate the two. Flat variable-length bytecode in `scrVarPub.programBuffer`. `waittill`/`notify`/`endon`/`wait`/`switch`/`self`/`level` are **dedicated opcodes**, not builtin calls. No separate line-number table — line numbers are recovered lazily by rescanning source against a stored `sourcePos` (`Scr_GetLineNum`, `scr_parser.cpp:406-416`); **skip porting this for now**, it's error-reporting-only.
3. **VM core**: `VM_ExecuteInternal()` (`scr_vm.cpp:1775-3480`, ~1,700 lines) — one dispatch over all opcodes, called by `VM_Execute` (thread/stack mgmt, `scr_vm.cpp:3482`) called by `Scr_ExecThread` (`scr_vm.cpp:1586`). Control-flow/stack/local-var/math opcodes are self-contained; entity-field-access opcodes (`OP_EvalSelfFieldVariable` etc.) and `OP_CallBuiltin*` reach into game-layer tables.
4. **Hello-world call chain**: `G_InitGame` (`g_main.cpp:1062`) → `Scr_InitSystem(1)` (`g_main.cpp:2606`, builds the builtin tables: `methods[104]` @ `g_scr_main.cpp:42` **and** `methods_2[166]` @ `g_scr_main.cpp:150` — 270 methods total — plus `functions[251]` @ `g_scr_main.cpp:324`) → `Scr_LoadLevel()` (`g_scr_main.cpp:601`) → loads `main` label of `maps/<mapname>.gsc` → `Scr_ExecThread` → `VM_Execute` → `VM_ExecuteInternal`. (All builtins this plan ports come from `functions[]`, not the `methods[]` tables — noted here so later steps don't assume `functions[251]` is the whole builtin surface if they ever need a method-style call.)
5. **Lowest-dependency builtins** (port first): `print`/`println` (`g_scr_main.cpp:328-329`), `isdefined`/`isstring`/`isarray`, `getdvar*`/`setdvar*` (`g_scr_main.cpp:363-370`), `assert`/`assertmsg`. **Avoid early**: `spawn` (pulls in the full entity/spawn subsystem), any `getaiarray`/`spawnturret`/vehicle/light/ragdoll builtin.
6. **Entity spawning**: `G_SpawnEntitiesFromString` (`g_spawn.cpp:1206`) → `G_ParseSpawnVars` → `SP_worldspawn()` → loop `G_CallSpawn()` (`g_spawn.cpp:963`) dispatching classname → spawn function (`SP_script_model`, `SP_trigger_*`, table from `g_spawn.cpp:~51`). **Unconfirmed**: exact `gentity_s` field layout — grep `g_local.h`/`g_shared.h` directly in Step 8, don't trust this summary.
7. **No prior-art reimplementation found** in the tree (no OpenWarfare/iw4x/gsc-tool references) — this is a from-scratch port, not adapting an existing OSS GSC VM.

## De-risking strategy

Hand-write bytecode for synthetic test programs and get the **VM interpreter running first** (Phase A), deferring the compiler (Phase B) — the interpreter's opcode surface is finite, enumerable, and testable in complete isolation with no dependency on real `.gsc` text. The compiler (yacc-derived grammar, full lexer) is higher-risk and only pays off once the VM's semantics are locked and testable against it. Real `.gsc` extraction (Step 1) runs **first and in parallel with nothing** — it's pure research, de-risks every later estimate, and costs almost nothing since the rawfile scanner already exists.

## Step graph

```
Step 1 (research: extract real .gsc)  ───────────┐
                                                   ▼
Step 2 (opcode enum + programBuffer)         Step 6 (lexer, real tokens)
        │                                         │
        ▼                                         ▼
Step 3 (VM core: stack/control/math)         Step 7 (parser → AST subset)
        │                                         │
        ▼                                         │
Step 4 (builtins: print/dvar/isdefined)           │
        │                                         │
        └─────────────────┬───────────────────────┘
                           ▼
              Step 8 (compiler: AST → bytecode,     Step 5 (minimal entity
              needs 3 + 4 + 7 — first join point)    struct + spawn subset,
                           │                          zero dependencies,
                           │                          runs any time)
                           └───────────┬───────────────────┘
                                       ▼
                        Step 9 (Scr_LoadLevel wiring: load+compile+
                                run a real level's main() on device —
                                needs 4 + 5 + 8, second join point)
                                       │
                                       ▼
                        Step 10 (device validation pass + gap doc)
```

**Parallel opportunities**: Step 1 has no dependencies — run it before/alongside anything. Steps 2 and 5 touch unrelated files and can run in parallel once Step 1 lands (Step 5 doesn't actually need Step 1, it can start immediately). Step 6 needs Step 1 (real source to tokenize against) but not Step 2/3/4. Steps 3→4 are strictly serial (builtins need a working dispatch loop). Step 7 needs Step 6. **Step 8 needs Step 3, Step 4, AND Step 7** (not just 3 and 7 — Step 8's own exit criteria exercise builtins, which only exist after Step 4; see Step 8's context brief) — the first hard join point. Step 9 needs Step 4, Step 5, and Step 8 — the second join point.

---

## Step 1 — Extract and study real GSC source from a loaded zone

**Depends on:** nothing
**Model tier:** default
**Parallel with:** Step 2, Step 5

### Context brief

This is pure research with a small deliverable, not new subsystem code. `ScanZoneRawFiles()`/`DumpZoneRawFiles()` (`android/app/src/main/cpp/kisak_zone_rawfile_android.{h,cpp}`) already recover plain-text files embedded in a zone by signature-scanning for the serialized `Load_RawFile` pattern (`{name=-1, len, buffer=-1, name\0, content\0}`), not by filename — see `DescribeZoneRawFiles` for the existing logging shape. The zone loader's `.gsc` handling has never been checked. Every later step's scope estimate depends on what real `.gsc` looks like — do not skip this or guess.

### Tasks

- [ ] Using the existing `run-kisakcod-android` skill/driver, get `killhouse.ff` (or `code_post_gfx.ff`/`common.ff` — check which zone actually carries `maps/killhouse.gsc`) onto the dev machine (it's already loaded at runtime per the `Zone de mission` log line documented in project memory; use the same in-process rawfile dump path, or add a temporary diagnostic log listing every rawfile name in a loaded zone if `.gsc` isn't obviously named).
- [ ] Confirm whether `.gsc` files are captured by the EXISTING `ScanZoneRawFiles()` scan as-is, or whether COD4 serializes scripts through a different asset type (check the `rawfile` name list from a real load — search for any entry ending `.gsc`). If not captured, identify what asset type/offset scripts actually use before continuing (this may itself require a small parser addition — scope that as a new sub-task if found, don't silently expand this step's PR to include a new asset type unless it's trivial).
- [ ] Dump at least 2-3 real `.gsc` files to disk (ideally: the level's own `maps/killhouse.gsc` if it exists, plus a smaller common one) and read them. Note actual syntax used: which control structures (`if`/`while`/`for`/`switch`), whether `waittill`/`notify`/threading (`thread`/`endon`) appear in the simplest observed script, array/struct usage, and roughly how many lines the simplest real `main()` is.
- [ ] Write findings to `plans/gscript-real-source-notes.md` (new file, not this plan) — the raw text samples plus your syntax notes. This file is the reference every later step's "does our subset cover this" check reads.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c   # or the connected device's serial
# Confirm no crash, and that the diagnostic output (log or dumped files) contains at least
# one real .gsc file's actual source text, not just a filename.
```

### Exit criteria

- `plans/gscript-real-source-notes.md` exists with at least 2 real, human-readable `.gsc` source excerpts and a syntax-feature checklist.
- A clear yes/no on whether `ScanZoneRawFiles()` already captures `.gsc` as-is.

### Rollback

Pure research + one new markdown file — nothing to roll back beyond deleting the file. Safe to redo from scratch if data extraction goes down a wrong path.

---

## Step 2 — Opcode enum + programBuffer primitives

**Depends on:** nothing
**Model tier:** default
**Parallel with:** Step 1, Step 5

### Context brief

New files `kisak_script_vm_android.h`/`.cpp` (naming matches the existing `kisak_<subsystem>_android` convention). This step is data structures only — no execution logic yet, no compiler, nothing wired into the build's gameplay loop. Reference: `Opcode_t` enum at `src/script/scr_vm.h:16-157` (139 entries, `OP_End=0` to `OP_count=0x8A`).

**Bytecode reader primitives — file correction**: these are **NOT** in `scr_readwrite.cpp` (that file is savegame serialization only — it literally opens with `#error This file is for SinglePlayer only` and marshals the variable stack to/from a `MemoryFile` for save/load; porting from it would silently build the wrong subsystem). The actual operand readers the VM dispatch loop uses are `Scr_ReadCodePos`/`Scr_ReadInt`/`Scr_ReadUnsignedShort`/`Scr_ReadFloat`/`Scr_ReadVector` in **`src/script/scr_vm.cpp:1637-~1770`** (immediately above `VM_ExecuteInternal`), plus the raw opcode fetch (`opcode = *(unsigned char*)fs.pos++`) inside the dispatch loop itself. Port from there.

### Tasks

- [ ] Port `Opcode_t` as a C++ `enum class : uint8_t` (or `int32_t` if any opcode value assumption in later ports needs it — check the reference enum's underlying type) with all 139 entries, preserving exact names and numeric values from `scr_vm.h`.
- [ ] Port the bytecode buffer reader primitives (byte/short/int/float/vector reads with cursor advance) from `scr_vm.cpp:1637-~1770` (`Scr_ReadCodePos`/`Scr_ReadInt`/`Scr_ReadUnsignedShort`/`Scr_ReadFloat`/`Scr_ReadVector` — see file-correction note above, NOT `scr_readwrite.cpp`), adapted to a `std::vector<uint8_t>` buffer + cursor rather than the retail global `scrVarPub.programBuffer` pointer arithmetic.
- [ ] Write a `KisakScriptProgram` struct: `{ std::vector<uint8_t> bytecode; /* + whatever minimal metadata Step 3 turns out to need, e.g. entry-point offsets per function name — keep this minimal and let Step 3 extend it rather than over-designing now */ }`.
- [ ] Add `kisak_script_vm_android.cpp` to `CMakeLists.txt` (`target_sources`/the `add_library(kisakcod_android SHARED ...)` list, same pattern as `kisak_audio_android.cpp` was added this session).
- [ ] Host-side sanity check (this project's established workflow: prototype with `g++` before touching the NDK build) — write a throwaway `scratchpad` test that constructs a tiny hand-built byte buffer and confirms the reader primitives round-trip correctly (read back the same values written). A pure write-then-read-back-with-your-own-code round-trip can pass even if the primitives don't match `scr_vm.cpp`'s actual width/endianness/advance-order per opcode type — additionally hand-transcribe 2-3 known operand-read call sites from `scr_vm.cpp`'s dispatch loop (e.g. how `OP_JumpOnFalse` reads its jump target) and confirm your reader's behavior matches what that call site expects, not just internal self-consistency.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build   # must compile clean on all 4 ABIs, no gameplay wiring yet
```

No device test needed for this step — it's inert until Step 3 calls it.

### Exit criteria

- New files compile clean as part of the existing `kisakcod_android` shared library on all 4 ABIs.
- Opcode enum has exactly 139 named entries matching `scr_vm.h`.
- A committed host-side test (or at minimum a scratchpad script + its output pasted into the PR description) proves the reader primitives are byte-order/width correct.

### Rollback

New files only, nothing else touched — revert the CMakeLists.txt line and delete the two new files.

---

## Step 3 — VM core: stack, locals, control flow, arithmetic

**Depends on:** Step 2
**Model tier:** strongest (this is the highest-complexity single step in the whole plan — a ~1,700-line reference function being ported and TRIMMED to a subset, get it reviewed carefully)

### Context brief

Port `VM_ExecuteInternal()` (`src/script/scr_vm.cpp:1775-3480`) — but only the opcode subset that doesn't touch entities or builtins yet: stack push/pop, local variable get/set, jumps (`OP_Jump`, `OP_JumpOnFalse`, etc.), arithmetic/comparison operators, function call/return **for script-defined functions only** (no `OP_CallBuiltin*` yet — that's Step 4). Explicitly stub/assert-unreachable on any opcode outside this subset for now (entity fields, builtins, `waittill`/`notify`/`switch`/threading) — this step proves the dispatch loop and math/control-flow semantics are correct, nothing more. Test with **hand-written bytecode buffers**, not real compiled scripts — the compiler doesn't exist yet (Step 8).

### Tasks

- [ ] Design the VM's runtime value type — check first whether `KisakExprValue` (`kisak_menu_expression_android.h` — **confirm with grep, do not trust this plan blindly**, already used by the UI expression evaluator for tagged int/float/string values via `MakeInt`/`MakeFloat`/`MakeString`) fits GScript's needs before creating a new one. It has no entity-ref variant today — decide explicitly whether that's added now or deferred, and say which in the PR description. If reuse doesn't fit cleanly, a new `KisakScriptValue` is fine, but justify why.
- [ ] Port the call-stack / execution-thread state struct (a trimmed `scriptthread_t`/similar from `scr_vm.h`) — script-function call/return, local variable frame.
- [ ] Port `VM_ExecuteInternal`'s dispatch loop for the subset above, as a `KisakScriptVm::Execute(const KisakScriptProgram&, uint32_t entryOffset)` (or similar) entry point.
- [ ] Any opcode outside the subset: log clearly and stop execution (don't silently no-op — Step 4/8 need to know exactly what's missing when they hit it).
- [ ] **Decide and implement a runtime-error model now, not by accident later.** The reference VM uses pervasive `setjmp`/`longjmp` (`g_script_error[g_script_error_level]`, `scr_vm.cpp:1817+`) for script runtime errors (type mismatches, undefined vars, etc.). Porting `longjmp` semantics 1:1 isn't required — a simple `bool`/result-code return or a C++ exception is fine — but pick one explicitly and document it, so Step 8/9 don't have to invent error handling under pressure once real scripts start hitting edge cases.
- [ ] Build a small **test-only bytecode assembler helper** (not a full compiler — just enough to hand-emit opcode+operand sequences without manually computing byte offsets by hand each time, in particular jump-target offsets for the `if`/`else` and loop test programs below). This is real, load-bearing scope for this step, not incidental — size it as such.
- [ ] Host-side test harness (again: prototype in `scratchpad` with `g++` before NDK), using the assembler helper above: hand-assemble bytecode for at least 3 synthetic programs — (a) integer arithmetic + a local variable, (b) an `if`/`else` branch, (c) a `while` or `for` loop with a counter — and confirm correct final state.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness output for all 3 synthetic programs pasted into the PR description —
# this step has no device-visible behavior yet (nothing calls it from the render loop).
```

### Exit criteria

- All 3 synthetic test programs produce mathematically correct final state.
- Reference-fidelity note in the PR: which opcodes were ported, which were explicitly stubbed for Step 4+.

### Rollback

New files only (extends Step 2's files or adds `kisak_script_vm_android.cpp`'s executor half) — not called from anywhere in the app yet, safe to revert wholesale.

---

## Step 4 — Builtin function dispatch + minimal builtin table

**Depends on:** Step 3

### Context brief

Adds `OP_CallBuiltin*` opcode handling to the VM from Step 3, plus a small builtin function table. Reference: `functions[251]` table (`src/game/g_scr_main.cpp:324`, `BuiltinFunctionDef`) — port ONLY the lowest-dependency entries identified in research: `print`/`println` (`g_scr_main.cpp:328-329`), `isdefined`/`isstring`/`isarray`, `getdvar*`/`setdvar*` (`g_scr_main.cpp:363-370`), `assert`/`assertmsg`. Wire `getdvar`/`setdvar` to the SAME dvar store the menu system already uses (`SetKisakUiDvar`/`GetKisakUiDvar` in `kisak_menu_expression_android.h` — **not** `kisak_menu_state_android.h`, verify with grep) rather than inventing a second dvar store — confirm this is a reasonable fit before wiring (the menu dvar store's value type should already be compatible if Step 3 reused `KisakExprValue`). Wire `print`/`println` to `__android_log_print` with the `KisakCODAndroid` tag, matching every other subsystem's logging convention.

### Tasks

- [ ] Port a small `BuiltinFunctionDef`-equivalent table (name → C++ function pointer) for exactly: `print`, `println`, `isdefined`, `isstring`, `isarray`, `getdvar`, `getdvarint`, `getdvarfloat`, `setdvar`, `assert`, `assertmsg`.
- [ ] Add `OP_CallBuiltin*` handling to the VM dispatch (Step 3's `Execute`), resolving function name → table entry → call with the VM's current argument stack.
- [ ] Extend the host-side synthetic-bytecode test harness with a 4th program that calls `print` and `setdvar`/`getdvar`, confirming the log line and dvar round-trip.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh install a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh launch a38b2d7c
# Still not called from the real game flow — verification is the host-side harness output.
# Device install/launch here is only a build-doesn't-break-the-app smoke check.
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Synthetic program calling `print`/`setdvar`/`getdvar` produces correct output and correctly round-trips through the menu system's existing dvar store.
- No crash on install/launch (this step shouldn't be reachable from any real code path yet, but confirm it doesn't break the build/app regardless).

### Rollback

Additive to Step 3's files — revert the builtin table + dispatch addition, VM core from Step 3 remains intact.

---

## Step 5 — Minimal entity struct + `G_SpawnEntitiesFromString` subset

**Depends on:** nothing (can start immediately, parallel with Steps 1-4)

### Context brief

**Before writing any code**, `grep -rn "gentity_s" src/game/g_local.h src/game/g_shared.h` (and any other `g_*.h`) to confirm the actual field layout — the research pass for this plan could NOT locate it and flagged it explicitly as needing direct confirmation. Do not assume the summary above is complete. Reference: `G_SpawnEntitiesFromString` (`src/game/g_spawn.cpp:1206`) → `G_ParseSpawnVars` → `SP_worldspawn()` → `G_CallSpawn()` (`g_spawn.cpp:963`) dispatching classname → spawn function via a table starting around `g_spawn.cpp:51`. This step's job is a MINIMAL `gentity_s`-equivalent (origin, classname, angles, script-relevant fields only — not the full retail struct, which drags in the 131-file `game/` directory) plus enough of the spawn dispatch to construct that minimal entity for `script_model`/`trigger_*` classnames from a real `map_ents` string, **without wiring it to the script VM yet** (that's Step 9). This step's own exit criteria should be satisfiable by comparing its output against `ParseModelEntities`'s existing output on the same zone (same entity count/positions) — proving the new path is a faithful, more general replacement candidate before Step 9 makes it live.

### Tasks

- [ ] Confirm `gentity_s`'s real field layout via direct grep (see above) — record findings in the PR description before proceeding, this determines the struct's shape.
- [ ] Design `KisakScriptEntity` (or similar) — a minimal struct covering what `script_model`/basic `trigger_*` spawn functions actually touch (origin, angles, classname, model name, targetname, and any `script_*` fields referenced by `Scr_SetEntityField`, `g_spawn.cpp:1215`).
- [ ] Port `G_CallSpawn`'s classname→function dispatch table, but only for `script_model`/`misc_model`/one basic `trigger_*` class — not the full table.
- [ ] Run this new path against the SAME `map_ents` string `ParseModelEntities` (`kisak_world_scene_android.cpp`) already parses for a real loaded zone (e.g. killhouse), and diff the two entity lists (count + positions) for the classnames both handle.
- [ ] **Do not wire this into `BuildWorldScene` or replace `ParseModelEntities` in this step** — it stays a standalone, tested-in-isolation module until Step 9 decides how (and whether) to integrate it.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
# Diagnostic log comparing new-path vs ParseModelEntities entity counts/positions for the
# same real zone — paste both counts into the PR description, they should match for the
# classnames the new path handles.
```

### Exit criteria

- New spawn-dispatch path produces the same entity count/positions as `ParseModelEntities` for `script_model`/`misc_model` on a real loaded zone.
- `gentity_s` field assumptions are grep-confirmed, not inherited from this plan's summary.

### Rollback

Standalone new module, not wired into the render/world-scene path — revert freely, `ParseModelEntities` is untouched and still the live path.

---

## Step 6 — GScript lexer (real source → tokens)

**Depends on:** Step 1 (needs real `.gsc` text to tokenize against)

### Context brief

**Lexer location — file correction**: `src/script/scr_parser.cpp` contains **no lexer** — it's source-position/line-number bookkeeping (`Scr_GetLineNum`, `Scr_AddSourceBuffer`, `Scr_ReadFile`), i.e. exactly the deferred line-number-recovery machinery from fact #2, not tokenization. The real lexer is **`yylex` in `src/script/scr_yacc2.cpp`** (2,119 lines, **flex-generated**, not hand-written — there is no `Scr_GetToken`). The grammar/`yyparse` lives separately in `src/script/scr_yacc.cpp` (7,993 lines, also generated) — that's Step 7's concern, not this step's.

Since `scr_yacc2.cpp` is machine-generated flex output (not idiomatic hand-written C to read and port line-by-line), the practical approach is almost certainly to **read it for the token TABLE and rules it encodes** (keywords, operators, literal formats, comment handling) and hand-write an equivalent tokenizer against that understanding, rather than transliterating generated code — this mirrors Step 7's own explicit allowance for a hand-written recursive-descent parser in place of the generated `yyparse`. Decide and document which approach this step actually takes.

Test against the REAL `.gsc` source samples captured in Step 1 (`plans/gscript-real-source-notes.md`), not synthetic snippets — a lexer that only handles hand-picked easy cases is not validated. Output: a token stream (type + value + source line) ready for Step 7's parser to consume. No AST, no compilation yet.

### Tasks

- [ ] Identify and port the lexer's token type enum and the tokenizing behavior from `scr_yacc2.cpp`'s `yylex` (see file-correction note above for the hand-written-equivalent approach).
- [ ] New files `kisak_script_lexer_android.{h,cpp}`, added to `CMakeLists.txt`.
- [ ] Tokenize every real `.gsc` sample from Step 1's notes file; for each, manually verify (by eye, in the PR description or a committed test-output file) that keywords, identifiers, string/number literals, and operators are correctly classified — pay particular attention to whatever syntax features Step 1 flagged as present (e.g. if real scripts use `thread`/`waittill`, the lexer must at least tokenize those keywords correctly even though the parser won't handle them until later).
- [ ] Host-side test harness, same pattern as prior steps.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness: tokenize every Step 1 sample, diff/paste output for review.
```

### Exit criteria

- Every real `.gsc` sample from Step 1 tokenizes without lexer errors.
- Token stream for at least one full sample is included in the PR description for manual review.

### Rollback

New standalone files, not wired to anything else — revert freely.

---

## Step 7 — Parser subset: real tokens → AST

**Depends on:** Step 6

### Context brief

Port a **deliberately restricted** grammar subset from `scr_yacc.cpp`'s `yyparse` (the generated grammar, 7,993 lines — see Step 6's file-correction note; `scr_yacc2.cpp` is the lexer, not the grammar) plus AST node shapes from `scr_parsetree.cpp` — function definitions, simple statements (assignment, `if`/`else`, `while`/`for`, function calls, `return`), and expressions (arithmetic, comparison, logical). **Explicitly defer**: `switch`, `waittill`/`notify`/`endon`/`wait`/threading, arrays/structs beyond what's trivially needed, and any COD-specific syntax sugar (e.g. `self animscript`, weapon/anim-tree references) found in Step 1's real samples but not needed for the SIMPLEST real script identified there. The goal is the smallest grammar subset that parses the simplest real `main()` from Step 1, not full language coverage.

### Tasks

- [ ] Re-read Step 1's notes; pick the SIMPLEST real script (or a hand-trimmed version of one, noted as such) as this step's concrete target — the thing this step's exit criteria are graded against.
- [ ] Port (or hand-write, since the original is yacc-generated and yacc/bison may not be available/desired in this toolchain — a **hand-written recursive-descent parser producing the same AST shape** is an acceptable and likely preferable substitute; note this decision explicitly in the PR) a parser for the grammar subset above.
- [ ] Port the AST node types from `scr_parsetree.cpp` (trimmed to what the subset grammar produces).
- [ ] New files `kisak_script_parser_android.{h,cpp}`.
- [ ] Parse the chosen target script end-to-end into an AST; dump the AST structure (even as debug text) into the PR description or a committed test-output file.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness: parse the chosen target script, dump AST, paste into PR.
```

### Exit criteria

- The chosen target real script (from Step 1) parses without errors into a complete AST.
- Grammar subset boundaries (what's deliberately unsupported) are explicitly documented in the PR description.

### Rollback

New standalone files — revert freely, Step 6's lexer is unaffected.

---

## Step 8 — Compiler: AST → bytecode, validated against Step 3's VM

**Depends on:** Step 3 (VM to run the output), **Step 4 (builtins — see note below, this dependency is not optional)**, Step 7 (AST to compile from) — **first hard join point**
**Model tier:** strongest

### Context brief

Port `scr_compiler.cpp`/`scr_compiler2.cpp`'s AST→bytecode emission, but only for the AST node types Step 7's grammar subset actually produces, emitting only opcodes Step 3/4's VM actually implements. This step is where the two independent tracks (VM core + compiler front end) meet — expect friction here (an AST construct that doesn't map cleanly to the ported opcode subset, or a needed opcode Step 3 stubbed out). Resolve friction by extending Step 3's VM in this same step if the gap is small (a missing arithmetic opcode), or by flagging a follow-up step if it's a genuinely new subsystem (don't silently scope-creep this step into re-doing Step 3).

**Step 4 is a real prerequisite, not just Step 3**: this step's own exit criteria require running a script whose observable output is `print`/`setdvar` calls — those are builtins, delivered by `OP_CallBuiltin*` dispatch which Step 3 explicitly stubs out and Step 4 implements. Do not start this step until Step 4 is merged, even though the step graph shows it as parallel-eligible with Step 4 on paper — the two are only independent until this step's verification is reached.

### Tasks

- [ ] Port the AST→bytecode emitter for exactly the node types Step 7 produces.
- [ ] New files `kisak_script_compiler_android.{h,cpp}`.
- [ ] Compile Step 7's target script's AST into bytecode; run it on Step 3/4's VM.
- [ ] Confirm the compiled-and-executed script produces the SAME observable effects (e.g. `print`/`setdvar` calls, if the target script has any) as manually tracing the source would predict.
- [ ] If the target script from Step 1/7 doesn't exercise `print`/`setdvar`/any builtin, deliberately compile and run a small hand-picked or hand-trimmed real script that does, so this step has an observable pass/fail signal beyond "didn't crash."

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh install a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh launch a38b2d7c
# Host-side harness first (compile+run target script, verify output), THEN a device smoke
# test if this step wires a manual trigger (e.g. a debug menu item or launch-time hook) —
# only add such a hook if it's cheap; otherwise host-side validation is sufficient for this
# step and device wiring happens properly in Step 9.
adb -s a38b2d7c logcat -d -b crash | tail -20
```

### Exit criteria

- Full pipeline (lex → parse → compile → execute) runs end-to-end on at least one real `.gsc` script (or faithful trimmed excerpt) with observable, correct output.
- Any VM gaps discovered and fixed here are noted in the PR description; any deferred are filed as explicit follow-up notes in `plans/gscript-real-source-notes.md`.

### Rollback

New standalone files; if VM extensions were needed, they're additive to Step 3/4's files — revert the compiler files first, then any VM extension only if it's not needed elsewhere.

---

## Step 9 — Wire `Scr_LoadLevel`-equivalent: real level load + `main()` on device

**Depends on:** Step 4 (builtins), Step 5 (entity spawn subset), Step 8 (full compile pipeline) — **second hard join point**

### Context brief

This is the step that actually changes observable game behavior. Reference call chain: `G_InitGame` → `Scr_InitSystem` → `Scr_LoadLevel` → load `maps/<mapname>.gsc`'s `main` label → `Scr_ExecThread`. Port a trimmed equivalent, hooked into `StartWorldLoad`/`BuildWorldScene` (`kisak_android_native_renderer.cpp`/`kisak_world_scene_android.cpp`) — the exact hook point is this step's design decision, informed by where `ParseModelEntities` currently runs. Decide (and document the decision): does the script-driven Step 5 entity spawn path REPLACE `ParseModelEntities` outright, run ADDITIONALLY alongside it (risk: double-spawned entities — needs a de-dup strategy), or stay OPT-IN behind a flag for this step and become default in Step 10 once validated? Given the established "additive, don't break what works" pattern this whole port has followed, **default to keeping `ParseModelEntities` as the fallback and making the script path opt-in/parallel for this step** — flipping the default is a Step 10 decision made after real device validation, not this step's job.

**Reminder of the objective's scope cut**: script-spawned entities (via Step 5's `KisakScriptEntity`) are validated here by log counts/effects, not by rendering — this step does not need to route them through the XModel-instancing path `ParseModelEntities` feeds. If that turns out to be trivial once you're in this code, a stretch task is fine, but it is not required for this step's exit criteria.

### Tasks

- [ ] Port a trimmed `Scr_LoadLevel`-equivalent: given a mapname, find+extract its `.gsc` via Step 1's confirmed extraction path, run it through Steps 6/7/8's pipeline, execute `main()` via Step 3/4's VM.
- [ ] Wire Step 5's entity-spawn subset as the script's `spawn`-equivalent builtin (extending Step 4's builtin table) — this is likely the FIRST real use of the `spawn` builtin the earlier research explicitly flagged as "avoid early," now unavoidable and properly scoped by everything before it.
- [ ] Hook this into the existing world-load flow as documented in the context brief (opt-in/parallel, not a hard replacement).
- [ ] Add clear logcat diagnostics (matching every other subsystem's `KisakCODAndroid`-tagged style): script found/not found, compile success/failure with line info, `main()` execution result, entity spawn count from the script path vs. `ParseModelEntities`.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh install a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh launch a38b2d7c
# Navigate to a real mission (killhouse via New Game -> Recruit, per this session's
# established navigation coordinates in run-kisakcod-android's SKILL.md) and capture:
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "script\|Scr_LoadLevel\|main()"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
adb -s a38b2d7c exec-out screencap -p > /tmp/step9_check.png   # visual sanity check
```

### Exit criteria

- A real level's `.gsc` `main()` executes on device without crashing.
- Logcat shows concrete evidence of script execution (not just "loaded", actual builtin calls observed — e.g. a `print`/`setdvar` the real script contains, or the entity-spawn count from the script path).
- World still renders normally (device screenshot, no regression vs. pre-step baseline).

### Rollback

If device validation fails or destabilizes the app, the opt-in/parallel design from the context brief means reverting to `ParseModelEntities`-only is a one-flag change, not a structural revert — confirm this is actually true before merging (part of this step's own review).

---

## Step 10 — Device validation pass + documented gap list

**Depends on:** Step 9

### Context brief

No new subsystem code — this step is validation, cleanup, and honest documentation of what's NOT covered, so the next blueprint (or next session) doesn't re-discover known gaps from scratch. Matches this project's established pattern of closing out a milestone with a clear device-validated commit message and a memory update (see `project_kisakcod_android.md` in the auto-memory store for the format every prior milestone this session used).

### Tasks

- [ ] Run the full pipeline against 2-3 different real levels (not just killhouse), documenting pass/fail and any script that hits an unsupported grammar/opcode/builtin.
- [ ] Write a clear "what's covered / what's not" section: grammar subset boundaries (from Step 7), opcode coverage (Step 3/4), builtin coverage (Step 4), entity classnames covered (Step 5) — concrete enough that a future blueprint for `switch`/`waittill`/threading/full entity spawn/AI could start from this document instead of re-researching the whole VM.
- [ ] Update the project's auto-memory (`project_kisakcod_android.md`) with this milestone, following the exact structure every prior gameplay milestone this session used (bold header + what/how/validated-on-device line + gotchas).
- [ ] Confirm no regressions in prior milestones (viewmodel, hitscan, map_ents props, audio, HUD stick) — these don't have automated tests, so this means a manual device pass touching each: fire the weapon, hear/see the effects, move with the stick HUD visible, confirm props still render.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
# Manual device pass per the tasks above — this step is graded on the resulting
# documentation quality and the absence of regressions, not new automated checks.
```

### Exit criteria

- `plans/gscript-real-source-notes.md` (or a new `plans/gscript-vm-gaps.md`) has a complete, concrete "covered vs. not" list.
- Auto-memory updated.
- All 5 prior gameplay milestones manually re-confirmed working on device in the same pass.

### Rollback

Documentation-only step; if a regression is FOUND during the manual pass, that's a bug in an earlier step's PR, not this step — file it against the responsible step and fix there, don't patch it inline here.

---

## Plan-level notes for whoever executes this

- **Every step's device testing must account for the two device-testing gotchas** documented in "What already exists" above (tap-vs-swipe, zero-distance-swipe-as-instant-tap) — re-derive them from scratch only if the `run-kisakcod-android` `SKILL.md` doesn't already cover them by the time you read this (it should, but skills can drift).
- **Do not attempt the world-geometry culling fix** while working on this plan, even if you notice the double-sided draw during profiling. It was tried and reverted this same session (commit `6e00a89`) — the brush/terrain winding mismatch is real and unrelated to anything in this plan.
- **This plan does not include AI** (`src/game/actor_*.cpp`). If Step 10's gap list makes AI look tractable, that's a NEW blueprint, not an extension of this one — the two are genuinely separate 20k+-line subsystems.
- Every step reuses this session's established host-then-device workflow: prototype in `/tmp/claude-*/scratchpad` (or the repo's own scratch conventions) with `g++` before touching the NDK build, exactly like the zone/fastfile loader's own development process (documented in `project_kisakcod_android.md`).
