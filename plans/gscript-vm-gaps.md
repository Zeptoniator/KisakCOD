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
ScriptThreadCallPointer` remain unimplemented, deliberately, per that
blueprint's own scope cut), self/level/game object model (`GetSelf/
GetLevel/GetGame/EvalFieldVariable/EvalFieldVariableRef/ScriptMethodCall/
ScriptMethodThreadCall` — see the entity/object-model blueprint entry
below; `GetAnim/EvalLevelFieldVariable/EvalAnimFieldVariable/
EvalSelfFieldVariable/EvalLevelFieldVariableRef/EvalAnimFieldVariableRef/
EvalSelfFieldVariableRef/CallBuiltinMethod*/ScriptMethodCallPointer/
ScriptMethodThreadCallPointer` remain unimplemented, deliberately, per
that blueprint's own scope cut — the fast-path field variants are
retail-only optimizations this port collapsed into the two generic
opcodes above, not a coverage gap).
**83 of 138 real opcodes implemented** (counted directly against
`KisakScriptOpcode`'s 139 entries minus the `OP_count` sentinel). **This
count is UNCHANGED by the switch/loop-control blueprint** (`plans/android-
gscript-switch-control-flow.md`, 2026-07-21, all 5 steps) — the first
blueprint this session to add zero new opcodes: `switch`/`case`/`default`
desugars into the EXISTING `equality`/`JumpOnTrue`/`jump` opcodes (a
split-dispatch layout, not retail's own jump-table `OP_switch`/
`OP_endswitch` encoding), and `break`/`continue` reuse the EXISTING
`jump`/`jumpback` opcodes via compiler-only bookkeeping (a context stack),
with no VM/runtime concept at all.

**Builtins** (`KisakScriptBuiltinTable()`, 12 entries): `print`, `println`,
`isdefined`, `isstring`, `isarray` (now genuinely checks the `Array` type —
was an always-false stub before the arrays blueprint), `getdvar`,
`getdvarint`, `getdvarfloat`, `setdvar`, `assert`, `assertmsg`, `spawn`
(step 9, non-retail signature — see below).

**Grammar** (`kisak_script_parser_android.cpp`): function definitions with
params, blocks, `if`/`else`/`else if`, `while`, `for`, `return`, assignment
(simple + `+=`/`-=`/`*=`/`/=`/`%=`, including on a field target — see
entity-model row below), postfix/prefix unary (`++`/`--`/`!`/`-`/
`~`), all arithmetic/comparison/logical binary operators, bare function calls,
field access (`.field`, read AND write — see entity-model row below),
GSC's no-dot method-call syntax (`<object-expr> <bareword>(args)`, both
non-threaded — already covered pre-entity-model — and threaded, `<expr>
thread funcName(args);`/`<expr> thread path\file::func(args);`, a new
`MethodThreadCallStatement` node — see entity-model row below),
`#include path\segment;` (parsed and dropped, no AST content), arrays
(`[]` literal — always empty, no populated-literal syntax exists in real
GSC; `expr[key]` subscript, chainable, both read and assignment-target
contexts; `expr.size`, its own AST node kind, deliberately separate from
the entity-model field-access mechanism), bare threading (`thread
funcName(args);` / `thread path\file::func(args);` — no object prefix;
`wait <expr>;`, including a fix to promote `wait` from a plain Identifier
to a real keyword, a genuine gap from the original lexer step),
`switch (subject) { case V: ...; default: ...; }` with real C-style
fallthrough (a case with no `break` falls into the next case's
statements — confirmed against real cargoship_extract.gsc:189's own
no-break cascade), both string- and integer-cased switches against the
same subject type, an optional `default:` clause in any source position,
and loop `break`/`continue` for both `while` and `for` — see the
switch/loop-control row below.

**Entity classnames** (`kisak_script_entity_android.h`, map_ents dispatch):
`script_model`, `trigger_multiple` — 2 of the real spawn table's ~25 entries
(`s_bspOrDynamicSpawns`/`s_bspOnlySpawns`, `g_spawn.cpp:45,71`).

**Self/level/game object model** (`plans/android-gscript-entity-model.md`,
2026-07-21, all 6 steps): a new `Object` value type (shared_ptr-backed
string-keyed field map, same reference-semantics shape as `Array`);
`self`/`level`/`game` as real, generic field-storage objects — field read
AND write both work (plain and compound, `+=`/`-=`/etc — confirmed against
real corpus, `self.baseaccuracy *= .8;`), including a field that itself
holds an array (`level.foo[key] = x`), with real AUTO-VIVIFICATION when the
field was never previously initialized (`level.fogvalue["near"] = 100;`
with no prior `level.fogvalue = [];` — cargoship's own real first `main()`
statement); object-prefixed calls, both non-threaded (`self setModel(...)`)
and threaded (`level thread maps\file::main();`, the exact construct that
was the shared blocker for killhouse/cargoship going into this blueprint),
rebinding the callee's `self` to the receiver; `self` inheritance through
ordinary (non-rebinding) calls, including through the local-function-
pointer-call path. Explicit, deliberate scope cuts (all still rejected,
with specific errors, not silently accepted): `anim` (unused by the real
corpus at every failure boundary across all five blueprints this session);
entity builtin-method calls (`entity.hide()`-style — no game/entity-
simulation backing exists); `waittill`/`notify`/`endon`/`waittillmatch`/
`waittillframeend` (real thread-suspension semantics this port's
synchronous VM has no scheduler for — confirmed these are lexer keywords
that structurally cannot be misparsed as a call under any of this port's
grammar, so the rejection is a legible message, not a safety-critical
check); the local-`FunctionRef`-pointer tier for object-prefixed calls (no
`OP_ScriptMethodCall(Thread)Pointer` opcode exists); assignment to a bare
object keyword (`level = x;`); cross-`Execute()`-call persistence of
`level`/`game` (freshly allocated per `Execute()` call — unobservable
today, since exactly one `Execute()` call happens per script trigger).

**Switch/case/default + loop break/continue** (`plans/android-gscript-
switch-control-flow.md`, 2026-07-21, all 5 steps): `switch (subject) {
case V1: ...; case V2: ...; default: ...; }` desugared via a split-
dispatch layout (a dispatch prologue of `EvalLocal/literal/OP_equality/
OP_JumpOnTrue` comparisons, entirely separate from a contiguous body
block emitted in source order) — this separation is what makes real
fallthrough automatic, confirmed against cargoship_extract.gsc:189's own
real no-break cascade (matching the first case runs every subsequent
case's body too) and against `ally_sas_woodland_smg_mp5.gsc:26-41`'s
break-stops-fallthrough shape. Both string- and integer-cased switches,
a `default:` clause in any source position (confirmed working both last
and non-last), a switch subject that is itself an arbitrary expression
(evaluated exactly once into a hidden compiler-synthesized local slot).
`break;`/`continue;` for both `while` and `for` loops via a compile-time-
only context stack (no VM/runtime concept) — `for`'s `continue;` correctly
still runs the increment clause before re-testing the condition; nested
loops/switches each correctly target their OWN nearest enclosing
construct. Zero new VM opcodes (see the opcode-count note above).
Deliberate scope cuts: case labels are a plain int/string literal only
(no computed values, matching every real corpus example); multiple case
labels sharing one body (`case "a": case "b": ...`) is not supported (no
real corpus usage found); switching on a non-Int/non-String subject
(e.g. an Array or Object) is a clean `RuntimeError` via the existing
`OP_equality` type check, matching retail's own restriction.

## Not covered (deliberate, documented boundaries — not bugs)

| Area | Gap | Why deferred | Where it would land |
|---|---|---|---|
| ~~VM~~ | ~~Entity/object model: no `OP_GetSelf/GetLevel/GetGame`, no `OP_Eval*FieldVariable`, no `OP_ScriptMethodCall(Thread)`~~ | **COVERED (self/level/game field access + object-prefixed calls)** as of `plans/android-gscript-entity-model.md` (2026-07-21, all 6 steps) — see the "Covered" paragraph above. `GetAnim`/`OP_CallBuiltinMethod*` remain deliberately unimplemented (`anim` unused by the real corpus; no entity/game-simulation backing exists for builtin-method dispatch). | — |
| ~~Parser+VM~~ | ~~Bare `thread`/`wait`~~ | **COVERED (bare/self-implicit forms only)** as of `plans/android-gscript-threading.md` (2026-07-21, all 5 steps) — `thread funcName(args);`/`thread path\file::func(args);` (same-file and cross-file, both confirmed on real corpus) implemented as a documented, synchronous-inline simplification (no true concurrency — this port's VM has no per-frame re-entry point to suspend into); `wait <expr>;` validates its argument (matching retail's Int/Float/negative-rejection checks exactly) but is a documented no-op, no real delay modeled. `wait` was ALSO promoted from a plain Identifier to a real lexer keyword as part of this (a genuine pre-existing gap, not a deliberate omission — the lexer's own header comment already cited a real `wait .1;` example from this corpus). | — |
| ~~Parser+VM~~ | ~~Object-prefixed `<expr> thread funcName(...)`~~ | **COVERED** as of `plans/android-gscript-entity-model.md` — the exact shared blocker killhouse/cargoship converged on (`level thread maps\<file>::main();`) now compiles AND executes end to end, confirmed against both real levels via the full cross-file pipeline. `waittill`/`waittillmatch`/`waittillframeend`/`notify`/`endon` remain explicitly deferred (real thread-suspension semantics this port's synchronous VM has no scheduler for) — confirmed these are lexer keywords, structurally impossible to misparse as a call under any of this port's grammar, so a legible "deferred subsystem" message is all that's needed, not a safety mechanism. | — |
| Parser | `/# ... #/` — real COD4 GSC's debug-block delimiter (brackets debug-only code, e.g. AI pain-debugging hooks). **Newly discovered** (threading blueprint step 4, bog_a.gsc line 106) — never identified by any of the four prior blueprints' own research phases. This port's lexer currently tokenizes `/` and `#` as separate operators, not the paired delimiter retail's real grammar treats them as. | Not anticipated by any prior research pass — a genuine gap, not a deliberate scope cut | Likely a small, independent, mechanical fix (skip the bracketed block, similar to how `#include` is parsed-and-dropped today) — plausibly NOT entangled with the entity model at all, a possible easy win before that blueprint lands |
| ~~VM~~ | ~~`switch`/`case`/`default`/`break` (as a switch, not a loop-break — loops have no `break`/`continue` either)~~ | **COVERED** as of `plans/android-gscript-switch-control-flow.md` (2026-07-21, all 5 steps) — see the "Covered" paragraph above. Confirmed against real corpus: cargoship's own line-189 switch (the first real, measured first-hit gap any level had reached, per the entity-model blueprint's own step 5) now compiles and executes end to end, advancing the file 13 more lines to the next, already-documented, unrelated gap (`#using_animtree`, see its own row below). | — |
| ~~VM~~ | ~~Arrays~~ | **COVERED (plain-variable arrays AND arrays-on-entity-fields)** as of `plans/android-gscript-arrays.md` (2026-07-21, all 5 steps, plain variables) + `plans/android-gscript-entity-model.md` (2026-07-21, step 4, arrays on self/level/game fields, INCLUDING auto-vivification on first indexed write to an unset field). `Array` value type (shared_ptr-backed map, reference semantics matching retail's ref-counted `VAR_POINTER` arrays), `[]`/`[key]`/`.size` fully working for read AND write, both int- and string-keyed against the same array, on plain locals AND on object fields alike. Compound assignment on an array element (`arr[key] += v`) is still explicitly rejected (no real corpus usage found) rather than risking a double-key-evaluation miscompile — compound assignment on a plain FIELD (no array), by contrast, IS covered (no key-expression to double-evaluate). | — |
| VM | `OP_GetIString` (interned/localized strings) | No localization table; `&"KEY"` degrades to a plain `OP_GetString` (step 8) | Needs the real string/localize table, likely same effort as arrays |
| VM | Vectors (`OP_GetVector`, `OP_vector`) | No vector literal grammar (step 7 never disambiguated `(x,y,z)` from a parenthesized expr) | Parser + VM value-type work, moderate |
| ~~Parser~~ | ~~Namespaced calls (`path\file::func()`) and function pointers (`::func`)~~ | **COVERED** as of `plans/android-gscript-namespaced-calls.md` (2026-07-21, all 6 steps) — bare `::func`/same-file namespaced calls resolve via `FunctionRef`+`OP_GetFunction`/`OP_ScriptFunctionCallPointer`; cross-file namespaced calls resolve via `CompileGscZoneEntryPoint`'s qualified symbol table + cross-file backpatch, for any target file present in the SAME scanned zone. Still cannot resolve a call into a file absent from every zone this port ever scans (e.g. `maps\_blackhawk::main()`, a shared cross-mission script) — that is a data-availability limit, not a parser/compiler gap; see the plan's own Objective section. | — |
| Parser | `#using_animtree(...)` and any other `#`-directive besides `#include` | Not anticipated when step 7 wrote `SkipIncludeDirective` — first real discovery this step (hunted.gsc line 5) | Small, mechanical parser fix — **re-confirmed as a real, measured blocker a second time**: after the switch/loop-control blueprint (2026-07-21), cargoship's own line-189 switch now compiles cleanly and the file's NEW failure point (line 202) is this exact same construct, `#using_animtree("generic_human");` — the SAME gap hunted.gsc:5 already hits, not a new discovery. Now confirmed as the shared next blocker for 2 of 4 real levels. |
| ~~Parser~~ | ~~Array subscript syntax~~ | **COVERED**, see the VM row above. | — |
| ~~Compiler~~ | ~~Field access/assignment and method calls on `self`/`level`/`game`/`anim`~~ | **COVERED (self/level/game)** as of `plans/android-gscript-entity-model.md` — see the "Covered" paragraph above. `anim` remains rejected with a specific compile error (unused by the real corpus at every current failure boundary). | — |
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

### Re-run after `android-gscript-entity-model.md` (step 5, 2026-07-21)

Same 4 levels (killhouse re-added to this specific harness, which had only
covered cargoship/bog_a/hunted since the namespaced-calls era), after
self/level/game field access + object-prefixed calls moved from "not
covered" to "covered" above. **The plan's own prediction — a LARGE jump for
killhouse/cargoship specifically — confirmed precisely, not assumed**:

| Level | Method | New result | Delta |
|---|---|---|---|
| killhouse | host (full fastfile/rawfile/`CompileGscZoneEntryPoint` pipeline) + **real device** (same finding, see below) | Parse fails line 371: `level waittill ( "mission failed" );` | Line 221 → 371 (**+150 lines**) — past the shared cross-file blocker AND every field-access construct in between; lands on the FIRST genuinely out-of-scope construct (`waittill`, Scope Cut item 3 — anticipated, not a surprise) |
| cargoship | host | Parse fails line 189: `switch(level.jumptosection) { ... }` | Line 172 → 189 (**+17 lines**) — past the shared cross-file blocker AND `level.fogvalue["near"] = 100;`'s own real auto-vivification (line 10, confirmed no longer a blocker); lands on a genuinely NEW gap, `switch`/`case` (re-ranked above) |
| bog_a | host | **UNCHANGED**, line 106: `/#` | No change — the `/#` debug-block gap sits earlier in the file than anything this blueprint touches; confirmed unrelated, not a missed opportunity (this blueprint's own isolated test already proves bog_a.gsc:807's real `self set_force_color("c");` compiles and executes correctly) |
| hunted | host | Unchanged, line 5, `#`-directive | No change — correct, unrelated |

**Both entity-model-targeted files now land on constructs this blueprint
explicitly predicted or scoped out, not on anything mysterious.** Full
account, including exact real source lines and the auto-vivification
cross-check, in `plans/gscript-real-source-notes.md`'s entity-model-step-5
addendum.

### Re-run after `android-gscript-switch-control-flow.md` (step 4, 2026-07-21)

Same 4 levels, after switch/case/default + loop break/continue moved from
"not covered" to "covered" above. **The plan's own headline claim — that
cargoship's real line-189 switch blocker would resolve — confirmed
precisely**:

| Level | Method | New result | Delta |
|---|---|---|---|
| killhouse | host + real device (same finding) | Unchanged, line 371: `level waittill ( "mission failed" );` | No change — correct, unrelated to switch/loop-control |
| cargoship | host | Parse fails line 202: `#using_animtree("generic_human");` | Line 189 → 202 (**+13 lines**) — the real switch (lines 189-198) now compiles cleanly end to end; the new blocker is NOT a new discovery, it's the SAME already-documented `#using_animtree` gap `hunted.gsc:5` already hits |
| bog_a | host | Unchanged, line 106: `/#` | No change — the `/#` gap sits earlier than anything this blueprint touches; confirmed separately via an isolated snippet that bog_a's own real continue-in-for-loop idiom (bog_a_extract.gsc:613-619's structure) compiles and executes correctly |
| hunted | host | Unchanged, line 5, `#using_animtree(...)` | No change — correct, and now literally the SAME construct as cargoship's own new blocker |

**cargoship's real switch statement is confirmed fully resolved, not a
coincidental advance**: the new failure line sits only 4 lines past the
switch's own closing brace, with nothing else in between that could
account for it. Full account, including the isolated bog_a continue test,
in `plans/gscript-real-source-notes.md`'s switch-control-flow-step-4
addendum.

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

### Re-run after `android-gscript-entity-model.md` (step 6, 2026-07-21)

Confirmed on-device (killhouse, fresh app install + launch): world
rendering unchanged from the established baseline (vehicle body paint/camo
textures, viewmodel weapon, buildings/props/street all correct, screenshot-
identical). Crash buffer empty (`crashcheck` clean) throughout, including
after triggering `New Game` → `devmap killhouse`. Script pipeline:
`Step9 script 'maps/killhouse.gsc': COMPILATION ECHOUEE (1 erreurs, 0
fichiers chaines): maps/killhouse.gsc parse: line 371: 'waittill' is a
deferred subsystem, not yet supported by this grammar subset (plans/
android-gscript-entity-model.md, Scope Cut item 3)` — exactly matching
Step 5's host-confirmed finding, message and line both identical, no
discrepancy between the host pipeline and the real device zone-loader
path. World/mission zone loaded fully afterward (1684/1684 assets).

**Inconclusive this pass, same documented artifact as several prior
passes**: synthetic `adb shell input swipe` gestures for the HUD move-
stick and camera look did not produce a visible change across 2 attempts
each. This blueprint's commits never touched touch/render/input code (only
the GScript VM/compiler files), so this is judged the same synthetic-input
timing flakiness already documented multiple times in this file, not a
regression — flagged honestly rather than re-claimed as freshly verified.

### Re-run after `android-gscript-switch-control-flow.md` (step 5, 2026-07-21)

Confirmed on-device (killhouse, fresh app install + launch): world
rendering unchanged from the established baseline (buildings, vehicles
with correct opaque body/camo textures, viewmodel weapon, street props —
screenshot-confirmed once the world scene finished building, ~90s after
zone-asset load; an earlier screenshot taken mid-build correctly still
showed the main menu, not a regression). Crash buffer empty (`crashcheck`
clean) throughout. Script pipeline: `Step9 script 'maps/killhouse.gsc':
COMPILATION ECHOUEE (1 erreurs, 0 fichiers chaines): maps/killhouse.gsc
parse: line 371: 'waittill' is a deferred subsystem, ...` — exactly
matching Step 4's host-confirmed finding, message and line both
identical, no discrepancy. World/mission zone loaded fully (1684/1684
assets).

HUD move-stick/camera-look under synthetic input not independently
re-tested this pass — this blueprint's commits touch only the GScript
parser/compiler files (switch/case/break/continue), never touch/render/
input code, so re-verifying an already-well-established, unrelated code
path would add little beyond what prior passes already confirmed.

## Recommendation for whoever picks up the next blueprint

Re-ranked 2026-07-21 after the switch/loop-control blueprint (switch/case/
default + loop break/continue) shipped and was re-validated against real,
whole-file data on both host and device (see above). This is the SEVENTH
GScript blueprint this session (namespaced-calls, arrays, threading,
entity-model, switch/loop-control, in that order) — for the first time,
**two of the four real levels (cargoship, hunted) now converge on the
identical next construct**, the clearest, most measured signal since the
threading blueprint's own killhouse/cargoship convergence.

1. **`#using_animtree(...)` and other non-`#include` `#`-directives** —
   **the new top priority**, promoted from "low measured urgency" to a
   CONFIRMED CONVERGENT blocker: cargoship's own real line-202 construct
   (reached only after the switch/loop-control blueprint unblocked
   everything before it) is now the literal SAME construct as hunted.gsc's
   own long-standing line-5 blocker. A small, mechanical parser fix
   (`SkipIncludeDirective` currently only recognizes `#include`) —
   genuinely independent of every subsystem shipped so far.
2. **`waittill`/`notify`/`endon`/`waittillmatch`/`waittillframeend`** —
   killhouse's own current blocker (line 371, `level waittill(...)`,
   UNCHANGED across this blueprint), the FIRST real level to ever reach
   this construct as its measured next gap. Needs real thread-suspension
   semantics this port's synchronous VM has no scheduler for (the same
   architectural gap the threading blueprint already identified for bare
   `thread`/`wait`) — a genuinely new subsystem, not an opcode-sized
   patch. The entity/object model these depend on (a real
   `self`/addressable-object argument) now EXISTS (entity-model
   blueprint), so this is more tractable than it was before, but still
   needs its own scoping pass for the suspend/resume question.
3. **`/# ... #/` debug-block delimiter** — still open (threading
   blueprint step 4, bog_a.gsc:106), architecturally UNRELATED to
   anything shipped since, including this blueprint (confirmed zero
   visible difference to bog_a's real-file progress AGAIN — its own real
   continue-heavy loop code is confirmed working in isolation, but the
   whole-file blocker sits earlier in the file). Still a plausible quick,
   independent win for a future session.
4. `OP_GetIString` (localized strings), vectors (`OP_GetVector`/
   `OP_vector`), fuller entity spawn coverage — all still open, all still
   lower-measured-urgency than the three above (none has ever been the
   first-hit gap for any real level).
5. ~~Namespaced calls + function pointers~~ — **shipped**, see "Covered" above.
6. ~~Arrays (plain-variable AND on entity fields, incl. auto-vivification)~~
   — **shipped**, see "Covered" above.
7. ~~Bare threading (`thread`/`wait`)~~ — **shipped**, see "Covered" above.
8. ~~Entity/object model (self/level/game field access + object-prefixed
   calls)~~ — **shipped**, see "Covered" above.
9. ~~`switch`/`case`/`default` + loop `break`/`continue`~~ — **shipped**,
   see "Covered" above.

Full AI (`actor_*.cpp`) remains explicitly out of scope for all of the above
— a separate blueprint again, per the original plan's own note.
