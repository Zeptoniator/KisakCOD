# GScript VM port — coverage summary and gap list

Step 10 of `plans/android-gscript-vm-port.md` (device validation pass +
documented gap list). This is the "start here" reference for any future
blueprint touching GScript — grammar/opcode/builtin/entity coverage boundaries,
concrete real-script findings, and what's explicitly out of scope. Read this
before re-researching any of it from the decompiled reference tree.

Implementation lives in `android/app/src/main/cpp/kisak_script_{vm,entity,
lexer,parser,compiler}_android.{h,cpp}`, wired into `StartWorldLoad`
(`kisak_android_native_renderer.cpp`). Per-step design rationale and the full
research trail live in `plans/gscript-real-source-notes.md`'s step-by-step
addenda — this document is the condensed, forward-looking summary.

## Pipeline

`.gsc` text → lex (step 6) → parse (step 7) → compile (step 8) → execute
(steps 2/3/4) → optional `spawn()` builtin (step 9) → `KisakScriptEntity`
records (step 5). Fully wired end-to-end and running on real device data
(see "Real multi-level validation" below) — this is a working, if narrow,
GScript interpreter, not a prototype.

## Covered

**Opcodes** (`KisakScriptOpcode`, `kisak_script_vm_android.cpp`'s
`Interpreter::Run()`): value push (`GetUndefined/GetZero/GetByte/GetNegByte/
GetUnsignedShort/GetNegUnsignedShort/GetInteger/GetFloat/GetString`), locals
(`CreateLocalVariable/RemoveLocalVariables/EvalLocalVariableCached0-5+generic/
SetLocalVariableFieldCached0+generic/EvalLocalVariableRefCached0+generic/
SetVariableField`), call prologue (`PreScriptCall/voidCodepos/
SafeCreateVariableFieldCached/SafeSetVariableFieldCached0/checkclearparams/
clearparams`), script call/return (`ScriptFunctionCall/ScriptFunctionCall2/
Return/End/DecTop`), jumps (`JumpOnFalse/JumpOnTrue/JumpOnFalseExpr/
JumpOnTrueExpr/jump/jumpback`), arithmetic/comparison (`plus/minus/multiply/
divide/mod/bit_or/bit_ex_or/bit_and/shift_left/shift_right/equality/
inequality/less/greater/less_equal/greater_equal/inc/dec`), unary (`CastBool/
BoolNot/BoolComplement`), `NOP/abort`, builtin dispatch (`CallBuiltin0-5/
CallBuiltin`), arrays (`EmptyArray/EvalArray/EvalArrayRef/size` — see the
arrays blueprint entry below; `ClearArray`/the cached-local-read variants
remain unimplemented, deliberately, per that blueprint's own scope cut),
bare threading (`ScriptThreadCall/wait` — see the threading blueprint
entry below; `waittillFrameEnd/waittill/waittillmatch/notify/endon/
ScriptMethodThreadCall/ScriptThreadCallPointer/ScriptMethodThreadCallPointer`
remain unimplemented, deliberately, per that blueprint's own scope cut).
**76 of 138 real opcodes implemented** (counted directly against
`KisakScriptOpcode`'s 139 entries minus the `OP_count` sentinel).

**Builtins** (`KisakScriptBuiltinTable()`, 12 entries): `print`, `println`,
`isdefined`, `isstring`, `isarray` (now genuinely checks the `Array` type —
was an always-false stub before the arrays blueprint), `getdvar`,
`getdvarint`, `getdvarfloat`, `setdvar`, `assert`, `assertmsg`, `spawn`
(step 9, non-retail signature — see below).

**Grammar** (`kisak_script_parser_android.cpp`): function definitions with
params, blocks, `if`/`else`/`else if`, `while`, `for`, `return`, assignment
(simple + `+=`/`-=`/`*=`/`/=`/`%=`), postfix/prefix unary (`++`/`--`/`!`/`-`/
`~`), all arithmetic/comparison/logical binary operators, bare function calls,
field access (`.field`, read-only in the parser — write is compiler-rejected),
GSC's no-dot method-call syntax (`<object-expr> <bareword>(args)`),
`#include path\segment;` (parsed and dropped, no AST content), arrays
(`[]` literal — always empty, no populated-literal syntax exists in real
GSC; `expr[key]` subscript, chainable, both read and assignment-target
contexts; `expr.size`, its own AST node kind, deliberately separate from
the deferred `.field` entity-model boundary), bare threading (`thread
funcName(args);` / `thread path\file::func(args);` — no object prefix;
`wait <expr>;`, including a fix to promote `wait` from a plain Identifier
to a real keyword, a genuine gap from the original lexer step).

**Entity classnames** (`kisak_script_entity_android.h`, map_ents dispatch):
`script_model`, `trigger_multiple` — 2 of the real spawn table's ~25 entries
(`s_bspOrDynamicSpawns`/`s_bspOnlySpawns`, `g_spawn.cpp:45,71`).

## Not covered (deliberate, documented boundaries — not bugs)

| Area | Gap | Why deferred | Where it would land |
|---|---|---|---|
| VM | Entity/object model: no `OP_GetSelf/GetLevel/GetGame/GetAnim`, no `OP_Eval*FieldVariable`/`OP_Set*FieldVariableField`, no `OP_CallBuiltinMethod*` | Genuinely new subsystem (a real `gentity_s`-equivalent + field storage), not an opcode-sized gap | New blueprint: "entity field access" |
| ~~Parser+VM~~ | ~~Bare `thread`/`wait`~~ | **COVERED (bare/self-implicit forms only)** as of `plans/android-gscript-threading.md` (2026-07-21, all 5 steps) — `thread funcName(args);`/`thread path\file::func(args);` (same-file and cross-file, both confirmed on real corpus) implemented as a documented, synchronous-inline simplification (no true concurrency — this port's VM has no per-frame re-entry point to suspend into); `wait <expr>;` validates its argument (matching retail's Int/Float/negative-rejection checks exactly) but is a documented no-op, no real delay modeled. `wait` was ALSO promoted from a plain Identifier to a real lexer keyword as part of this (a genuine pre-existing gap, not a deliberate omission — the lexer's own header comment already cited a real `wait .1;` example from this corpus). | — |
| Parser+VM | **Object-prefixed** `<expr> thread funcName(...)` (needs a real `self`-binding mechanism) and `waittill`/`waittillmatch`/`waittillframeend`/`notify`/`endon` (every one requires a genuine `VAR_POINTER`-typed object argument) — explicitly deferred by the threading blueprint's own scope cut (~36 and ~52 real corpus sites respectively, vs. 133+80 covered). **Confirmed, post-threading, to be the CURRENT real blocker for 2 of 3 real levels**: killhouse (unchanged, line 221) and cargoship (advanced to line 172) both now hit the identical `level thread <target>::main();` construct as their next failure point. | Needs the entity/object model's `self` mechanism (a real addressable object value) before either can be meaningfully implemented — not an opcode-sized gap | Same blueprint as the VM entity model, see below |
| Parser | `/# ... #/` — real COD4 GSC's debug-block delimiter (brackets debug-only code, e.g. AI pain-debugging hooks). **Newly discovered** (threading blueprint step 4, bog_a.gsc line 106) — never identified by any of the four prior blueprints' own research phases. This port's lexer currently tokenizes `/` and `#` as separate operators, not the paired delimiter retail's real grammar treats them as. | Not anticipated by any prior research pass — a genuine gap, not a deliberate scope cut | Likely a small, independent, mechanical fix (skip the bracketed block, similar to how `#include` is parsed-and-dropped today) — plausibly NOT entangled with the entity model at all, a possible easy win before that blueprint lands |
| VM | `switch`/`case`/`default`/`break` (as a switch, not a loop-break — loops have no `break`/`continue` either) | Not attempted; `Opcode_t` has `OP_switch`/`OP_endswitch` unimplemented | New, small, standalone blueprint — genuinely independent of arrays/namespaced-calls/threading, none of which touched it; not yet the measured first-hit gap for any real level, so still low urgency |
| ~~VM~~ | ~~Arrays~~ | **COVERED (plain-variable arrays only)** as of `plans/android-gscript-arrays.md` (2026-07-21, all 5 steps) — `Array` value type (shared_ptr-backed map, reference semantics matching retail's ref-counted `VAR_POINTER` arrays), `[]`/`[key]`/`.size` fully working for read AND write, both int- and string-keyed against the same array. Explicit, deliberate scope cut: `level.field[key]`/`self.field[key]` are NOT covered — still rejected by the pre-existing entity-model compile-time check (`kEntityDeferred`), same as plain `.field` assignment; that requires the entity/object-model blueprint, not this one. Compound assignment on an array element (`arr[key] += v`) is also explicitly rejected (no real corpus usage found) rather than risking a double-key-evaluation miscompile. | — |
| VM | `OP_GetIString` (interned/localized strings) | No localization table; `&"KEY"` degrades to a plain `OP_GetString` (step 8) | Needs the real string/localize table, likely same effort as arrays |
| VM | Vectors (`OP_GetVector`, `OP_vector`) | No vector literal grammar (step 7 never disambiguated `(x,y,z)` from a parenthesized expr) | Parser + VM value-type work, moderate |
| ~~Parser~~ | ~~Namespaced calls (`path\file::func()`) and function pointers (`::func`)~~ | **COVERED** as of `plans/android-gscript-namespaced-calls.md` (2026-07-21, all 6 steps) — bare `::func`/same-file namespaced calls resolve via `FunctionRef`+`OP_GetFunction`/`OP_ScriptFunctionCallPointer`; cross-file namespaced calls resolve via `CompileGscZoneEntryPoint`'s qualified symbol table + cross-file backpatch, for any target file present in the SAME scanned zone. Still cannot resolve a call into a file absent from every zone this port ever scans (e.g. `maps\_blackhawk::main()`, a shared cross-mission script) — that is a data-availability limit, not a parser/compiler gap; see the plan's own Objective section. | — |
| Parser | `#using_animtree(...)` and any other `#`-directive besides `#include` | Not anticipated when step 7 wrote `SkipIncludeDirective` — first real discovery this step (hunted.gsc line 5) | Small, mechanical parser fix |
| ~~Parser~~ | ~~Array subscript syntax~~ | **COVERED**, see the VM row above. | — |
| Compiler | Field access/assignment and method calls on `self`/`level`/`game`/`anim` | Rejected with a specific compile error (step 8) — depends on the VM entity-model gap above | Same blueprint as VM entity model |
| Entities | Only `script_model`/`trigger_multiple` — no `trigger_once`, `trigger_hurt`, `trigger_use`, `light`, `misc_turret`, actors (`actor_*`), items, vehicles, etc. | Step 5 explicitly scoped to "one basic trigger class" + script_model | New blueprint: "full entity spawn" |
| Entities | `spawn(classname, x, y, z)` is NOT retail's signature (`spawn(classname, origin)`, a vector) and returns an opaque Int handle, not a usable entity reference | No vector literal grammar, no entity value type (both above) | Resolved once vectors + entity model land |
| — | Full AI (`src/game/actor_*.cpp`, ~26,700 lines) | Always out of scope for this entire plan, not just this step | A separate ~20k+-line blueprint, per the plan's own plan-level notes |

## Real multi-level validation

### Original pass (step 10 of the VM port, 2026-07-20)

Ran the full pipeline against **4 real levels** (exceeds the "2-3" task):

| Level | Method | Result |
|---|---|---|
| killhouse | **real device**, `StartWorldLoad` wiring | Parse fails line 27: `default_start( ::inside_start );` — bare function-pointer literal (`::`) |
| cargoship | host (pulled `.ff`, same fastfile/rawfile/compiler pipeline) | Parse fails line 10: `level.fogvalue["near"] = 100;` — array subscript on a field |
| bog_a | host | Parse fails line 43: `maps\bog_a_fx::main();` — bare namespaced call |
| hunted | host | Parse fails line 5: `#using_animtree("generic_human");` — undirected `#`-directive |

**Every one of the 4 real levels' own main scripts fails to compile end to
end**, each at a different but equally real, correctly-diagnosed construct —
zero crashes, zero silent misdispatch. Three of four (killhouse, bog_a, and
by strong inference most other SP missions, since `default_start`/
`add_start( ..., ::funcname, ... )` is the standard mission-init idiom) hit
the **namespaced-call/function-pointer gap first or very early**.

### Re-run after `android-gscript-namespaced-calls.md` (step 6, 2026-07-21)

Same 4 levels, same method per level, after namespaced calls/function
pointers moved from "not covered" to "covered" above:

| Level | Method | New result | Delta |
|---|---|---|---|
| killhouse | **real device**, `StartWorldLoad` + `CompileAndRunScriptFromZone` | Compile fails line 204: `level.weaponClipModels = [];` — array literal | Line 27 → 204 (177 lines further); the plan's own hoped-for line 209 (`maps\_blackhawk::main()`, a missing-file error) sits just beyond an EARLIER, different, out-of-scope gap (arrays) that this specific script also happens to hit first — not reached this pass |
| bog_a | host, same `CompileGscZoneEntryPoint` path | Compile fails line 71: `level.weaponClipModels = [];` — array literal | Line 43 → 71 (28 lines further); **the plan's target construct at line 43 (`maps\bog_a_fx::main()`) now resolves cleanly**, along with `maps\_javelin::init()` (line 44) and the 2-segment `maps\createfx\bog_a_audio::main()` (line 69) — all 3 confirmed present in bog_a.ff's own rawfiles before compiling, not assumed |
| cargoship | host, unchanged path (single-file, no namespaced call before its own failure point) | Still line 10, same message | No change — correct, this script's blocker was never the namespaced-call gap |
| hunted | host, unchanged path | Still line 5, same message | No change — correct, same reasoning |

**bog_a is the clean, direct proof this plan worked**: 3 real namespaced
calls that used to be unparseable now compile and link correctly, in a
script this port never modified except via the namespaced-calls plan
itself. killhouse shows real, if partial, progress (177 lines) but is
gated on the SAME array-literal construct one step earlier than hoped —
confirming arrays (not the originally-assumed `_blackhawk` cross-zone
resolution) are now the genuine next blocker for both of these two real
scripts. cargoship/hunted correctly unchanged, as predicted going in.

No VM/compiler bug was found in any of these — every rejection matches a
documented, intentional scope boundary above.

### Re-run after `android-gscript-arrays.md` (step 4/5, 2026-07-21)

Same 4 levels, after plain-variable arrays moved from "not covered" to
"covered" above. **A genuine surprise, investigated rather than silently
accepted** (per that plan's own Step 4 instruction): the original
expectation was "same failure LINE, different error CATEGORY" (parse error
→ the pre-existing entity-deferred compile rejection on the `level.field`
target) — based on an isolated, synthetic reproduction of that exact
construct, which DOES show that category change correctly in isolation.
But `CompileGscSource`/`CompileGscZoneEntryPoint` both parse a file's
ENTIRE `Program` before compiling any of it — so the real, whole-file
result is better than predicted: once `[]`/`[key]` parse, parsing sails
straight past every `level.field[key]` use in the file and only stops at
the NEXT genuinely unparseable construct.

| Level | Method | New result | Delta |
|---|---|---|---|
| killhouse | **real device**, `StartWorldLoad` + `CompileAndRunScriptFromZone` | Parse fails line 221: `expected ';', got 'thread'` | Line 204 → 221 (17 more lines, past every array construct in between) |
| bog_a | host | Parse fails line 89: `expected ';', got 'thread'` | Line 71 → 89 |
| cargoship | host | Parse fails line 158: `expected ';', got 'thread'` | Line 10 → 158 (148 more lines — the largest single jump of any step in either blueprint) |
| hunted | host | Unchanged, line 5, `#`-directive | No change — correct, unrelated gap |

**All three real levels now land on the exact same new gap**: `thread`
(see the threading row above — this is a genuine parser-grammar gap, not
just a VM/scheduler one). Isolated real-corpus snippets confirm the array
construct itself is correct everywhere it's used in the real files
(`C4_models[i] hide()`, `aa[i] hide()/notsolid()`, the full real
`tooslow_dialog = []; tooslow_dialog[0..3] = ...;` block including its own
init line, `targets[i] thread moveTargetDummy(...)`) — rejected (where
rejected) only by the SEPARATELY-tracked, correctly-named, unrelated gap
(entity method calls or threading), never by a confusing array-specific
error. Full account in `plans/gscript-real-source-notes.md`'s arrays-step-4
addendum.

### Re-run after `android-gscript-threading.md` (step 4/5, 2026-07-21)

Same 4 levels, after bare `thread`/`wait` moved from "not covered" to
"covered" above. **A genuinely mixed result, not a uniform advance** —
documented honestly rather than smoothed over:

| Level | Method | New result | Delta |
|---|---|---|---|
| killhouse | **real device**, `StartWorldLoad` + `CompileAndRunScriptFromZone` | **UNCHANGED**, line 221: `level thread maps\killhouse_amb::main();` | No change — correct, object-prefixed thread is out of this plan's scope, exactly as predicted |
| cargoship | host | Line 172: `level thread maps\cargoship_amb::main();` | Line 158 → 172 (14 more lines); lands on the **SAME object-prefixed thread construct as killhouse** |
| bog_a | host | Line 106: `/#` (a debug-block delimiter, see the new gap row above) | Line 89 → 106 (17 more lines); a **genuinely NEW gap**, not another thread/wait construct |
| hunted | host | Unchanged, line 5, `#`-directive | No change — correct, unrelated |

**Two convergent, confirming data points**: killhouse and cargoship now
hit the IDENTICAL next construct (object-prefixed `thread`), reinforcing
that this is genuinely the highest-value next target for those two real
scripts specifically, ahead of anything else. bog_a's own next blocker is
architecturally unrelated to the entity model (a lex/parse-level debug-
directive gap) — a plausible small, independent fix that doesn't need to
wait for the entity-model blueprint at all.

Full account, including the exact real source lines, in
`plans/gscript-real-source-notes.md`'s threading-step-4 addendum.

## Device regression pass

### Original pass (step 10 of the VM port, 2026-07-20/21)

Confirmed working on-device (killhouse, fresh app relaunch, 2026-07-20/21):
world rendering (255 static models / 12225 instances / 8694 surfaces,
matching every prior session's baseline exactly, zero visual regression),
menu navigation/touch (instant, reliable), move-stick HUD (visually
confirmed twice, independently, including after a full app restart),
map_ents static props (visible in every screenshot: parked vehicles,
containers, fencing — same as baseline), the step 9 script pipeline itself
(self-test + real-script rejection reproduced identically across two
separate app launches).

**Inconclusive this pass: hitscan fire and its audio.** Synthetic `adb shell
input swipe` gestures to the world's fire-detection zone (documented as
`input swipe X Y X Y <100ms>`, a near-zero-distance "tap") did not produce
the expected `Tir: ...`/`Tir: aucun impact` log line across many attempts —
including varied coordinates, a small non-zero-distance variant, and a full
app restart — while, on the same device and in the same session, menu taps
and the move-stick gesture (a real-distance, backgrounded swipe) both
registered reliably and repeatedly. This is judged a synthetic-input/
device-session reliability quirk, not a code regression: `FireWorldWeapon`/
`UpdateWorldCamera`'s fire-detection branch and the audio pipeline have not
been touched by any commit in this entire blueprint (steps 1-10 only ever
added new script-VM files and additive diagnostic hooks in `StartWorldLoad`,
never touched touch/render/audio code). Per this project's own standard —
"if you can't test the UI, say so explicitly rather than claiming success"
— this is flagged honestly rather than claimed as re-verified. A real
finger on the touchscreen would be the natural way to close this out if
certainty is ever needed; it is not blocking anything in this plan, since
the code path is unchanged.

### Re-run after `android-gscript-namespaced-calls.md` (step 6, 2026-07-21)

Confirmed working on-device (killhouse, same session as the multi-level
re-validation above): world rendering unchanged (screenshot-identical to
baseline, same buildings/props/vehicles), camera look + move-stick
movement both responsive, **HUD move-stick overlay confirmed visible**
this time round-trip (the first two capture attempts missed it purely on
adb round-trip timing between starting the held swipe and firing the
screenshot in a SEPARATE adb call — a single inlined `adb shell "input
swipe ... & sleep 1; screencap ..."` call caught it cleanly; not a
regression, a capture-timing artifact of using two separate adb
invocations). Hitscan fire mechanism itself fires cleanly and logs `Tir:
aucun impact (hors de portee)` on 2/2 synthetic-tap attempts (no crash,
confirms the fire-detection branch this plan never touched still runs) —
**no impact was registered on either attempt** despite aiming at a nearby
wall; consistent with this project's own prior finding that the fire path
is specifically flaky under synthetic input (unlike look/move, which
registered reliably both this session and every prior one) — flagged
honestly as inconclusive on IMPACT REGISTRATION specifically, not on
whether the mechanism runs (it does). Audio not independently
re-exercised this pass (gated on a registered impact, which didn't occur)
— code path unchanged since the original VM plan, same reasoning as above
applies.

### Re-run after `android-gscript-arrays.md` (step 5, 2026-07-21)

Confirmed on-device (killhouse, fresh app relaunch): world rendering
unchanged (screenshot-identical to the established baseline — same
buildings, vehicle body paint/camo textures from this session's earlier
texture fix, viewmodel weapon), camera look and general touch input both
responsive (confirmed via a real screenshot showing the view correctly
rotated after a swipe). Crash buffer empty. Script pipeline: `Step9 script
'maps/killhouse.gsc': COMPILATION ECHOUEE (1 erreurs, 0 fichiers chaines):
maps/killhouse.gsc parse: line 221: expected ';', got 'thread'` — exactly
matching the host-confirmed finding above, no discrepancy between the host
pipeline and the real device zone-loader path.

**Inconclusive this pass, same as every prior pass**: the HUD move-stick
overlay wasn't caught in this specific pass's screenshot attempts (2
tries, both via a single inlined `adb shell "input swipe ...; screencap
..."` command — the same technique that DID catch it cleanly in the
namespaced-calls plan's own device pass). General touch/camera
responsiveness IS confirmed working (the look-swipe screenshot shows a
correctly rotated view), and this blueprint's commits never touched
touch/render code, so this is judged the same capture-timing flakiness
already documented, not a regression — flagged honestly rather than
re-claimed as freshly re-verified, per this project's own standard.
Hitscan fire/audio not re-attempted this pass (already flagged inconclusive
twice before under synthetic input, unrelated code path, diminishing
verification value from a third attempt).

### Re-run after `android-gscript-threading.md` (step 5, 2026-07-21)

Confirmed on-device (killhouse, fresh app relaunch): world rendering
unchanged from the established baseline (vehicle body paint/camo textures,
viewmodel weapon, buildings/props all correct). Script pipeline: `Step9
script 'maps/killhouse.gsc': COMPILATION ECHOUEE (1 erreurs, 0 fichiers
chaines): maps/killhouse.gsc parse: line 221: expected ';', got 'thread'`
— exactly matching Step 4's host-confirmed finding, no discrepancy. Crash
buffer empty throughout.

**Fire/audio and the HUD move-stick were both actually confirmed working
this pass**, a nice change from the last two passes' inconclusive results:
a look-zone swipe unexpectedly also registered as a hitscan fire (`Tir:
impact a 1886 u`, with a real `AAudio stream ouvert` line right alongside
it — genuine audio playback, not just a logged intent), and a subsequent
held swipe caught the HUD move-stick ring cleanly in a screenshot together
with visible forward movement. None of this blueprint's commits touch
touch/render/audio code, so this is additional confirming evidence of no
regression, not something this blueprint can take credit for causing.

## Recommendation for whoever picks up the next blueprint

Re-ranked 2026-07-21 after the threading blueprint (bare `thread`/`wait`)
shipped and was re-validated against real, whole-file data (see above).
Two real levels (killhouse, cargoship) now converge on the identical next
construct — this is the clearest, most measured signal any blueprint in
this series has produced so far.

1. **Entity/object model** (`self`/`level`/`game`, field access, `spawn`
   returning something real) — **the clear top priority**, confirmed by
   two convergent real levels: killhouse (line 221) and cargoship (line
   172) now BOTH fail on the identical object-prefixed `thread <target>::
   main();` construct, which needs a real `self`-binding mechanism this
   port doesn't have. This is also the biggest single subsystem and the
   one every other real script eventually needs — `level.field`/
   `self.field` accesses are already correctly rejected today by the
   existing `kEntityDeferred` compile-time check, this blueprint doesn't
   need to do anything new to make that correct, it just needs to REPLACE
   the rejection with real behavior.
2. **Object-prefixed `thread`** (`<expr> thread funcName(...)`) and
   **`waittill`/`notify`/`endon`** — bundle this with the entity/object-
   model blueprint above rather than a separate threading follow-up, since
   all of them need the SAME `self`-binding mechanism the entity model
   would build anyway; the bare-thread blueprint already proved out the
   opcode-reuse pattern (`OP_ScriptThreadCall`, `OP_ScriptMethodThreadCall`'s
   real-retail equivalent) these would extend.
3. **`/# ... #/` debug-block delimiter** — **newly discovered** (threading
   blueprint step 4, bog_a.gsc:106), architecturally UNRELATED to the
   entity model (a lex/parse-level gap, likely a small mechanical fix). A
   plausible quick, independent win for a future session that wants
   something smaller than the entity-model blueprint — does not need to
   wait for it.
4. Arrays' own remaining gap: `level.field[key]`/`self.field[key]` (arrays
   ON entity fields) — resolved automatically once the entity/object model
   lands, since plain-variable arrays are already fully working; no
   separate work needed here.
5. `switch`/`case`/`default`/loop `break`/`continue` — still low measured
   urgency (never the first-hit gap for any of the 4 real scripts across
   three re-validation passes now).
6. ~~Namespaced calls + function pointers~~ — **shipped**, see "Covered" above.
7. ~~Arrays (plain-variable)~~ — **shipped**, see "Covered" above.
8. ~~Bare threading (`thread`/`wait`)~~ — **shipped**, see "Covered" above.

Full AI (`actor_*.cpp`) remains explicitly out of scope for all of the above
— a separate blueprint again, per the original plan's own note.
