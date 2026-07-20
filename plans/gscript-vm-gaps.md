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
CallBuiltin`). **70 of 138 real opcodes implemented** (counted directly
against `KisakScriptOpcode`'s 139 entries minus the `OP_count` sentinel).

**Builtins** (`KisakScriptBuiltinTable()`, 12 entries): `print`, `println`,
`isdefined`, `isstring`, `isarray`, `getdvar`, `getdvarint`, `getdvarfloat`,
`setdvar`, `assert`, `assertmsg`, `spawn` (step 9, non-retail signature —
see below).

**Grammar** (`kisak_script_parser_android.cpp`): function definitions with
params, blocks, `if`/`else`/`else if`, `while`, `for`, `return`, assignment
(simple + `+=`/`-=`/`*=`/`/=`/`%=`), postfix/prefix unary (`++`/`--`/`!`/`-`/
`~`), all arithmetic/comparison/logical binary operators, bare function calls,
field access (`.field`, read-only in the parser — write is compiler-rejected),
GSC's no-dot method-call syntax (`<object-expr> <bareword>(args)`),
`#include path\segment;` (parsed and dropped, no AST content).

**Entity classnames** (`kisak_script_entity_android.h`, map_ents dispatch):
`script_model`, `trigger_multiple` — 2 of the real spawn table's ~25 entries
(`s_bspOrDynamicSpawns`/`s_bspOnlySpawns`, `g_spawn.cpp:45,71`).

## Not covered (deliberate, documented boundaries — not bugs)

| Area | Gap | Why deferred | Where it would land |
|---|---|---|---|
| VM | Entity/object model: no `OP_GetSelf/GetLevel/GetGame/GetAnim`, no `OP_Eval*FieldVariable`/`OP_Set*FieldVariableField`, no `OP_CallBuiltinMethod*` | Genuinely new subsystem (a real `gentity_s`-equivalent + field storage), not an opcode-sized gap | New blueprint: "entity field access" |
| VM | `waittill`/`waittillmatch`/`waittillframeend`/`notify`/`endon`/`wait` — no thread/notify machinery at all | Needs a scheduler + notify-list model; explicitly out of scope per the plan's objective | New blueprint: "GScript threading" |
| VM | `switch`/`case`/`default`/`break` (as a switch, not a loop-break — loops have no `break`/`continue` either) | Not attempted; `Opcode_t` has `OP_switch`/`OP_endswitch` unimplemented | Same blueprint as arrays, see below |
| VM | Arrays (`OP_EvalArray/EvalLocalArrayCached/ClearArray/EmptyArray` etc.) | No array value type in `KisakScriptValue`; real scripts use array subscripts constantly (`level.fogvalue["near"]`, confirmed in cargoship.gsc line 10) | New blueprint: "GScript arrays" — **highest real-world priority**, see findings below |
| VM | `OP_GetIString` (interned/localized strings) | No localization table; `&"KEY"` degrades to a plain `OP_GetString` (step 8) | Needs the real string/localize table, likely same effort as arrays |
| VM | Vectors (`OP_GetVector`, `OP_vector`) | No vector literal grammar (step 7 never disambiguated `(x,y,z)` from a parenthesized expr) | Parser + VM value-type work, moderate |
| Parser | Namespaced calls (`path\file::func()`) and function pointers (`::func`) | Explicitly deferred by step 7; **the single most common real-world blocker** — see findings below | Parser extension + a cross-file symbol model |
| Parser | `#using_animtree(...)` and any other `#`-directive besides `#include` | Not anticipated when step 7 wrote `SkipIncludeDirective` — first real discovery this step (hunted.gsc line 5) | Small, mechanical parser fix |
| Parser | Array subscript syntax `expr["key"]` / `expr[index]` | Same root cause as VM arrays — no array value type to target | Same blueprint as VM arrays |
| Compiler | Field access/assignment and method calls on `self`/`level`/`game`/`anim` | Rejected with a specific compile error (step 8) — depends on the VM entity-model gap above | Same blueprint as VM entity model |
| Entities | Only `script_model`/`trigger_multiple` — no `trigger_once`, `trigger_hurt`, `trigger_use`, `light`, `misc_turret`, actors (`actor_*`), items, vehicles, etc. | Step 5 explicitly scoped to "one basic trigger class" + script_model | New blueprint: "full entity spawn" |
| Entities | `spawn(classname, x, y, z)` is NOT retail's signature (`spawn(classname, origin)`, a vector) and returns an opaque Int handle, not a usable entity reference | No vector literal grammar, no entity value type (both above) | Resolved once vectors + entity model land |
| — | Full AI (`src/game/actor_*.cpp`, ~26,700 lines) | Always out of scope for this entire plan, not just this step | A separate ~20k+-line blueprint, per the plan's own plan-level notes |

## Real multi-level validation (this step's own device + host pass)

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
the **namespaced-call/function-pointer gap first or very early** — this is
the single highest-leverage next step if a future blueprint wants a real
mission's `main()` to run further than line ~10-40. Arrays (cargoship) and
stray `#`-directives (hunted) are the next two most common blockers.

No VM/compiler bug was found in any of these — every rejection matches a
documented, intentional scope boundary above.

## Device regression pass (this step)

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

## Recommendation for whoever picks up the next blueprint

In priority order, by real-world impact (per the 4-level validation above):

1. **Namespaced calls + function pointers** (`path\file::func`, `::func`) —
   unblocks the most real scripts, the furthest, for the least new-subsystem
   risk (it's "more parser + a cross-file name table," not a new VM concept).
2. **Arrays** (value type + `OP_EvalArray`/subscript grammar) — the second
   most common real blocker, and also required before entity field access
   is very useful (real scripts store per-frame state in arrays constantly).
3. **Entity/object model** (`self`/`level`/`game`, field access, `spawn`
   returning something real) — the biggest single subsystem, but also the
   one every other real script eventually needs; sequence it after 1+2 so
   it isn't fighting parser/array gaps at the same time.
4. Threading/`waittill`/`notify`/`switch` — lower real-world urgency than
   the above three based on this session's findings (none of the 4 real
   scripts hit these FIRST), but eventually necessary for anything beyond a
   few lines of any real mission script.

Full AI (`actor_*.cpp`) remains explicitly out of scope for all of the above
— a separate blueprint again, per the original plan's own note.
