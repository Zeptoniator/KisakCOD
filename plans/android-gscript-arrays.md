# GScript arrays — blueprint

Direct mode (no branches/PRs — this repo works on `android-port-bootstrap` directly, same as both prior GScript blueprints). Fork remote `fork` = `github.com/Zeptoniator/KisakCOD.git` (writable); `origin` = upstream, read-only.

## Objective

Extend the GScript VM port (`plans/android-gscript-vm-port.md`, all 10 steps complete; `plans/android-gscript-namespaced-calls.md`, all 6 steps complete) to support **arrays** — the confirmed, highest-priority remaining gap per `plans/gscript-vm-gaps.md`'s recommendation list (re-ranked 2026-07-21 after namespaced calls shipped and was re-validated against real data).

Real corpus evidence (all re-confirmed this session, not stale):
- `maps/killhouse.gsc` now fails to compile at **line 204**: `level.weaponClipModels = [];` followed by `level.weaponClipModels[0] = "weapon_mp5_clip";` — an array literal assigned to a `level` field, then indexed.
- `maps/bog_a.gsc` fails at the **same construct**, line 71.
- `maps/cargoship.gsc`'s original finding (`level.fogvalue["near"] = 100;`, line 10) is the same gap on a different field.

**Every one of these three real failures is on a `level.<field>` array**, not a plain variable. This blueprint's own scope cut (justified below, mirroring the namespaced-calls plan's own `_blackhawk` cross-zone honesty precedent) is **plain-variable arrays only** — `x = []; x[0] = 1; y = x[0];` — explicitly NOT `level.field[key]` or `self.field[key]`, which need the (separate, not-yet-started) entity/object-field-access blueprint. This means **none of the three real level scripts above will get PAST their current failure line by this blueprint alone** — but the failure CATEGORY at that same line does genuinely change: today all three die with a PARSE error (the array grammar itself doesn't exist yet); after this blueprint, `[]`/`[key]` parse correctly and these same lines instead reach the compiler and hit the PRE-EXISTING, already-implemented entity-deferred rejection (`level`/`self` field assignment is a known, separately-tracked gap, not a new one this blueprint introduces). State the line-doesn't-move fact plainly in every status update — do not oversell it — but the parse-to-compile category change at that line is real, verifiable progress worth reporting honestly too, not something to flatten into "nothing happened."

Despite that, this blueprint is real, necessary, and shows genuine forward progress on real data:
- Plain-variable array usage is **extensive** in the real corpus (confirmed via `grep` against real `maps/killhouse.gsc`): `C4_models[i]`, `aa[i]`, `angles[0]`, `targetDummies[index]`, `targets[selected_target]`, `tooslow_dialog[0..3]`, and `lines[lines.size]` (array literal built via `.size`-indexed append, a very common real GSC idiom). All of these are currently unparseable (`unexpected operator '['`).
- Arrays are also a **prerequisite** for the eventual entity/field-access blueprint — real per-object state in GSC is frequently array-shaped (`level.fogvalue["near"]`, `level.weaponClipModels[0]`), so a working array value type is groundwork that blueprint will directly reuse.
- `.size` (a genuinely separate, already-cataloged opcode, `OP_size` — NOT the deferred generic field-access system) is used 7+ times in `killhouse.gsc` alone and is fully in scope here.

## Why this is a blueprint and not a single PR

New VM value type (with reference/shared semantics, unlike every existing `KisakScriptValue` variant which is trivially value-copyable) + new parser grammar (literal, subscript-as-rvalue, subscript-as-lvalue, a new postfix property access) + new compiler emission paths (read AND write) + a real design decision on which opcodes to implement and how, all before any real-corpus validation is possible. Same shape as both prior GScript blueprints: multiple non-trivial, independently-reviewable steps with a hard join point in the middle.

## What already exists (read before starting ANY step)

- **Lexer** (`kisak_script_lexer_android.{h,cpp}`): `[` and `]` already tokenize as operators (confirmed against real corpus in the original VM plan's step 6 — this is why today's failures are PARSE errors, "unexpected operator '['", not lex errors).
- **Parser** (`kisak_script_parser_android.{h,cpp}`): no grammar production for array literals, subscript, or `.size` today. `ParsePrimary`'s postfix-chain handling (the same place namespaced-calls' Step 2 added `::`/`\` handling) is the natural place to add `[key]` and `.size` postfix parsing. Existing `FieldAccessExpr` (`.field`) parsing is a useful reference for how a postfix chain is already structured — but `.size` must NOT reuse `FieldAccessExpr` or its downstream compiler handling (see Architecture facts below on why).
- **Compiler** (`kisak_script_compiler_android.{h,cpp}`): `IsEntityKeyword`/`kEntityDeferred` (`kisak_script_compiler_android.cpp:636-640`) reject `self`/`level`/`game`/`anim` field-assignment targets with a specific, documented compile error — this is the existing entity-model scope boundary. **Important, verified correction**: this is a COMPILE-TIME check on the assignment target, not a parse-time keyword rejection — `level.weaponClipModels` (and, with this blueprint, `level.weaponClipModels[0]`) parses FINE today as an ordinary `FieldAccessExpr`/(after this plan)`ArrayIndexExpr`; only the compiler's assignment-target handling rejects it, and only once the base expression has already parsed successfully. This blueprint's plain-variable-array scope means `level.weaponClipModels[0] = ...` will still correctly end up rejected by this EXISTING check — but only if Step 3's array-assignment wiring routes its base expression through the same entity-checked path field-assignment already uses (see Architecture fact 6 and Step 3's own context brief for the precise reasoning and the concrete risk of a shortcut silently bypassing this).
- **VM** (`kisak_script_vm_android.{h,cpp}`): `KisakScriptValue` variants today: `Undefined, Int, Float, String, CodePos, PreCodePos, FunctionRef` — all trivially copyable (no shared ownership, no destructors doing real work). `KisakScriptOpcode` already catalogs (from the original VM plan's step 2, ported 1:1 from `Opcode_t`, `scr_vm.h:16-157`) `OP_EmptyArray = 0x25`, `OP_ClearArray = 0x24`, `OP_EvalArray = 0x20`, `OP_EvalLocalArrayCached = 0x1F`, `OP_EvalLocalArrayRefCached0 = 0x21`, `OP_EvalLocalArrayRefCached = 0x22`, `OP_EvalArrayRef = 0x23`, `OP_size = 0x76` — all currently UNIMPLEMENTED in `Interpreter::Run()`'s dispatch (confirmed against `plans/gscript-vm-gaps.md`'s coverage table: 70/138 opcodes implemented, none of these 8 among them). The enum values/names are fixed and must not change; this blueprint implements dispatch for a SUBSET of them (see Architecture facts).
- **Real corpus already on disk this session**: `/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad/gsc_killhouse/killhouse/maps/killhouse.gsc` (real, device-extracted). `scratchpad/step10_levels/{cargoship,bog_a,hunted}.ff` (real, pulled zone files) — if these don't survive to a future session, re-derive via `adb pull` from the device (`/storage/emulated/0/Android/data/com.kisakcod.android/files/cod4/zone/english/<map>.ff`) or the host fastfile/rawfile pipeline already proven working in `scratchpad/step10_multilevel/`. Do not work from paraphrased excerpts.
- **Host test harness pattern**: every prior step across both blueprints prototypes with `g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp <test>.cpp <real .cpp files> <dvar_host_stub.cpp>` in a scratchpad directory before touching the NDK build. `scratchpad/script_compiler_test/dvar_host_stub.cpp` is the link stub for dvar-store functions builtins need. `scratchpad/run_all_tests.sh` builds+runs all 7 existing suites — re-run it after every step to confirm zero regressions; extend it with each new suite you add.

## Architecture facts locked in by research (2026-07-21)

Researched directly in `src/script/scr_vm.cpp` (real dispatch, `VM_ExecuteInternal`, not the setjmp-guarded cleanup switches at lines 1823/1883/1885 — same gotcha the original VM plan already documented, the REAL dispatch for arrays is at lines 2369-2424) and `src/script/scr_variable.cpp` (`Scr_EvalArray:2814`, `Scr_EvalSizeValue:2591`).

1. **Retail arrays are `VAR_POINTER`-typed, ref-counted objects living in a shared global variable pool** (`scrVarGlob.variableList`), the SAME pool self/level/game/anim object references live in (`Scr_AllocArray()` allocates a slot in that pool; array reads/writes go through `RemoveRefToObject`/ref-counting). This is a MUCH bigger, more general mechanism than a plain map — it's retail's whole scriptable-variable system. **Deliberate simplification for this port** (mirroring the `FunctionRef`/`OP_ScriptFunctionCallPointer` precedent, which gave the VM a clean, purpose-built representation instead of porting retail's mechanism byte-for-byte): give `KisakScriptValue` a NEW `Array` variant backed by `std::shared_ptr<std::map<KisakArrayKey, KisakScriptValue>>` (a self-contained, port-owned structure with no dependency on a generic entity/object system). `KisakArrayKey` is a small tagged value holding EITHER an `int64_t` or a `std::string` — confirmed necessary by real data: `level.fogvalue["near"]` uses a string key, `level.weaponClipModels[0]`/`angles[0]`/`C4_models[i]` use integer keys, against what is architecturally the SAME array type in retail (one associative structure, either key kind).
2. **Arrays are reference types in retail** (ref-counted `VAR_POINTER`): assigning an array to another variable (`y = x;`) shares the SAME underlying array — mutating through `y` is visible through `x`. `shared_ptr`-backed storage gives this for free in `KisakScriptValue`'s existing copy semantics (copying a `KisakScriptValue::Array` copies the `shared_ptr`, incrementing refcount, not deep-copying the map) — no special-casing needed elsewhere, but this IS a behavioral change from every other existing variant (all trivially value-copied today) and must be called out in the value-type's own comments, and covered by a host test (pass an array into a function, mutate it, confirm the caller's copy changed too).
3. **`OP_EmptyArray` is the ONLY array-literal opcode** — it always produces an empty array (`fs.top->u.pointerValue = Scr_AllocArray()`); there is no "array literal with initial elements" opcode anywhere in the 139-entry catalogue, and no populated-literal syntax (`[1,2,3]`) appears anywhere in the real corpus (confirmed via `grep` against real `killhouse.gsc` and the character/aitype `.gsc` files pulled alongside it) — real scripts always build arrays via `x = []; x[key] = value;` in a loop or sequence. Do not add populated-literal grammar; it doesn't exist in real GSC.
4. **`OP_size` (`.size`) is its own dedicated, single-operand opcode** (`Scr_EvalSizeValue`, `scr_variable.cpp:2591`) — completely separate from the generic field-access system (`OP_Eval*FieldVariable`) this port's compiler already defers. Real semantics: array → element count; string → `strlen`; anything else → a runtime error ("size cannot be applied to %s"). Because it's its own opcode (not routed through `OP_Eval*FieldVariable`), implementing `.size` does NOT require touching the deferred entity-model boundary at all — give it its OWN AST node kind in the parser (do not reuse `FieldAccessExpr`), so there is no risk of it being confused with, or accidentally un-blocking, the deferred `self`/`level`/`game`/`anim` field-access rejection.
5. **Retail's array-element-write path is `OP_EvalArrayRef` (establish a ref to the element) followed by the GENERIC setter `OP_SetVariableField`** — and this port ALREADY implements exactly that two-opcode shape, just for locals, not arrays: `OP_EvalLocalVariableRefCached0/Cached` sets `frame().refSlot`, and the ALREADY-IMPLEMENTED `OP_SetVariableField` (`kisak_script_vm_android.cpp:806-810`, `frame().locals[frame().refSlot] = pop()`; also the opcode `OP_inc`/`OP_dec` already emit through — confirmed NOT "uncommitted", do not describe it that way) then writes through whatever ref was last established. **Recommended design, matching both retail's real shape and this port's own existing local-variable-ref precedent**: implement the genuinely-unimplemented `OP_EvalArrayRef` to establish a ref to `array[key]` (`refSlot`-equivalent state needs to be EXTENDED — not replaced — to represent either a local-variable slot OR an array element; this is Step 3's concrete design task), then EXTEND `OP_SetVariableField`'s existing body with a new branch for the array-element case, leaving its current local-slot branch untouched. Only fall back to inventing a single combined "pop array+key+value, write directly" opcode if the ref-based extension proves genuinely awkward once attempted — and if so, that fallback opcode still needs a real, already-cataloged ID (never invent a new numeric value, matching this port's own established constraint: every opcode implemented so far, across both prior blueprints, reuses a real ID from the fixed 139-entry `Opcode_t` enum).
6. **Scope boundary vs. the (separate, future) entity/field-access blueprint**: this blueprint implements PLAIN-VARIABLE arrays only. `level.field`/`self.field` (with or without a trailing array subscript) already PARSE today as an ordinary `FieldAccessExpr` — the rejection is NOT a lexical/parse-time check on the bare `level`/`self` token, it is a COMPILE-TIME check on the assignment target (`kisak_script_compiler_android.cpp:636-640`, `IsEntityKeyword`/`kEntityDeferred`), reached only AFTER the base expression has already parsed successfully. Concretely: `level.weaponClipModels` parses fine right now; only the compiler's assignment-target handling rejects it. This has a real consequence for Step 3: the array-assignment-target compiler path must still route its BASE expression through the normal, entity-checked `EmitExpression` path (the same one a bareword/field assignment already goes through) — do not build a shortcut that evaluates an `ArrayIndexExpr`'s base directly, or `level.x[0] = v` could silently slip past the entity-deferred rejection instead of hitting it. Step 4's real-corpus validation must confirm the rejection still fires (see the corrected exit criteria below — the ERROR CATEGORY changes after this plan, from a parse error to this compile-time rejection, at the SAME line; that category change is real, positive progress this plan should claim honestly, not something to flatten into "nothing changed").

## De-risking strategy

Same as both prior blueprints: VM value type + a hand-assembled synthetic bytecode test first (de-risks the new `Array` variant and its reference semantics without needing the parser or compiler to exist yet), parser grammar in parallel (independent), then a compiler join point, then real-corpus validation, then device.

## Step graph

```
Step 1 (VM: Array value type + OP_EmptyArray/OP_EvalArray/OP_size)
Step 2 (Parser: [] literal, [key] subscript, .size)          } run in parallel — no shared files, no output dependency
                    |
                    v
Step 3 (JOIN: array-element assignment — VM ref/set opcode(s) + compiler wiring for read AND write paths)  [strongest]
                    |
                    v
Step 4 (Real-corpus validation: killhouse/bog_a/cargoship re-run, hand-written array test script, reference-semantics test)
                    |
                    v
Step 5 (Device validation + gap-list update + memory update)
```

---

## Step 1 — VM: `Array` value type + `OP_EmptyArray`/`OP_EvalArray`/`OP_size`

**Depends on:** nothing (parallel-eligible with Step 2)
**Model tier:** default

### Context brief

Read "Architecture facts" points 1-4 above in full before starting. You're extending `kisak_script_vm_android.{h,cpp}` (the same files `FunctionRef`/`OP_GetFunction` were added to in the namespaced-calls plan's Step 1 — read that diff via `git show` on the relevant commit for the exact pattern to follow: a new `KisakScriptValueType` enum entry, a new field on `KisakScriptValue`, a factory function, `Truthy()`/`Describe()`/`AsString()` switch extensions, then VM dispatch cases).

This step does NOT touch the parser or compiler — it proves the VM-level mechanics with hand-assembled bytecode, exactly like the original VM plan's Step 3 and the namespaced-calls plan's Step 1 did.

### Tasks

- [ ] Add `Array` to `KisakScriptValueType`. Add a field to `KisakScriptValue` holding `std::shared_ptr<std::map<KisakArrayKey, KisakScriptValue>>` (define `KisakArrayKey` as a small tagged struct/variant of `int64_t` or `std::string` with `operator<` so it works as a `std::map` key — document the ordering choice, e.g. all-int keys before all-string keys, or whatever is simplest; ordering only matters for iteration, GSC array iteration order is not a behavior any real script in the corpus depends on so far, confirm this assumption holds by checking whether `foreach`-style iteration exists in the grammar today — it does not (foreach was explicitly never implemented per this port's own grammar coverage notes), so ordering is genuinely unobservable right now).
- [ ] Add `static KisakScriptValue Array()` (or similarly named) factory that allocates a fresh empty map via `std::make_shared`.
- [ ] Extend `Truthy()` (an array is truthy — confirm against retail: `Scr_EvalBoolNot`/`Scr_CastBool`'s handling of `VAR_POINTER`/arrays if easily checked, else default to "always true" as the safe, documented assumption, matching how `FunctionRef` was made unconditionally truthy in the namespaced-calls plan), `Describe()` (e.g. `"array[%zu]"` with element count, for debug/print output — check `print(someArray)` doesn't appear in the real corpus expecting specific formatting; if it does, match it, else this is free-form diagnostic text), `AsString()` (define a clear, documented behavior — retail likely errors or returns something format-specific; the safe default matching this port's existing `FunctionRef::AsString()` precedent is to return an empty string, not synthesize fake content).
- [ ] Implement `OP_EmptyArray` in `Interpreter::Run()`: push a new empty `Array` value (this port's simplified version of the real dispatch at `scr_vm.cpp:2420-2424` — no `Scr_AllocArray()`/global pool, just a fresh `shared_ptr<map>`).
- [ ] Implement `OP_EvalArray`: pop the array and the key (this port controls both emission and dispatch, so the exact stack order between the two is this step's own call — document whichever is chosen; it does not need to mirror retail's specific `fs.top`/`fs.top-1` layout, only the TYPE-CHECKED KEY HANDLING below needs to match retail), converting the popped key value to a `KisakArrayKey` (`Int` → int64 key, `String` → string key, anything else → a `RuntimeError`, matching retail's `Scr_EvalArray`'s type-checked key handling). **Missing-key read pushes `Undefined`, not an error** — confirmed via retail (`Scr_FindArrayIndex`, `scr_variable.cpp:3486`, returns the miss slot; `Scr_EvalVariable(0)`, `scr_variable.cpp:1678`, reads the undefined sentinel for that slot) and required for correctness: the already-ported `isdefined(arr[key])` builtin is an idiomatic real-GSC pattern that depends on a miss returning `Undefined` rather than erroring.
- [ ] Implement `OP_size`: pop one value; if `Array`, push `Int` = map size; if `String`, push `Int` = string length; else `RuntimeError`. Matches retail's `Scr_EvalSizeValue` for these two cases exactly. **Documented, conscious divergence**: retail has a THIRD case (a non-array `VAR_POINTER`, e.g. an entity reference, returns size `1` rather than erroring, `scr_variable.cpp:2606`) — this port has no non-array pointer/entity value yet, so that case cannot arise today; leave a one-line comment flagging it so a future entity-model blueprint revisits whether its new value type(s) need the same `size == 1` fallback rather than falling through to this step's `RuntimeError` branch.
- [ ] Do NOT implement `OP_ClearArray`, `OP_EvalLocalArrayCached`, `OP_EvalLocalArrayRefCached0`, or `OP_EvalLocalArrayRefCached` in this step — `OP_EvalArrayRef` IS Step 3's join-point responsibility (see Architecture fact 5's now-recommended ref+existing-`OP_SetVariableField` design) and should NOT be implemented here even partially, to keep the two steps' scope boundary clean; `OP_ClearArray` (removing a key) and the cached-local read variants are not needed for this blueprint's scope (no real corpus usage of key-removal was found; the cached-local-read opcodes are a compiler-side optimization this port's simpler compiler doesn't need, mirroring the `OP_ScriptThreadCallPointer` drop from the namespaced-calls plan's Step 3) — leave them logged as unsupported, matching this port's established "specific error, not silent misdispatch" pattern for every other uncovered opcode.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_array_test"
# Write a test harness following script_funcref_test/test_funcref.cpp's exact
# pattern: hand-assemble bytecode using script_vm_test/kisak_script_asm_test.h
# (extend it with an emptyArray()/evalArray()/size() helper the same way
# getFunction() was added for FunctionRef), verify:
#   - OP_EmptyArray produces an Array value
#   - write via direct map manipulation in the test (no compiler yet) + OP_EvalArray reads it back
#   - two KisakScriptValues sharing one Array (copy-constructed) both see a
#     mutation made through either one (proves shared_ptr reference semantics)
#   - OP_size on an array and on a string
#   - OP_EvalArray with a wrong-typed key -> RuntimeError, not a crash
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_array_test/test_array_vm.cpp" \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  -o "$SCRATCH/script_array_test/test_array_vm" && "$SCRATCH/script_array_test/test_array_vm"
bash "$SCRATCH/run_all_tests.sh"   # zero regressions across all 7 pre-existing suites
.claude/skills/run-kisakcod-android/driver.sh build   # clean on all 4 ABIs
```

### Exit criteria

- `Array` value type implemented with documented reference (shared_ptr) semantics.
- `OP_EmptyArray`/`OP_EvalArray`/`OP_size` implemented and host-tested, including the reference-semantics test.
- All pre-existing host suites still pass. NDK build clean on 4 ABIs.

### Rollback

Purely additive to `kisak_script_vm_android.{h,cpp}` (new enum entry, new field, new dispatch cases) — revert by removing the added code; nothing else depends on it yet.

---

## Step 2 — Parser: `[]` literal, `expr[key]` subscript, `expr.size`

**Depends on:** nothing (parallel-eligible with Step 1)
**Model tier:** default

### Context brief

Read `kisak_script_parser_android.cpp`'s `ParsePrimary` (the postfix-chain loop that already handles `.field` and, since the namespaced-calls plan, `::`/`\`) before starting — this is where `[key]` and `.size` postfix handling belongs, appended to the same chain. Read "Architecture facts" points 3-4 above.

### Tasks

- [ ] Add `ArrayLiteralExpr` to `KisakAstNodeKind` (no children/text needed — `[]` is always empty, per Architecture fact 3). Parse: after consuming `[`, if the very next token is `]`, produce `ArrayLiteralExpr`; if NOT (i.e., there's content between the brackets), this is being parsed as an array SUBSCRIPT on the wrong primary (the `[` immediately after an expression means subscript, not a literal) — make sure the grammar only recognizes `[]` (a literal) when `[` appears where a PRIMARY expression is expected (not immediately after another expression, which is the subscript case below); if a non-empty bracket pair is ever encountered where a literal would be expected, emit a clear parse error citing this plan (populated array literals don't exist in real GSC, per Architecture fact 3 — don't silently accept `[1,2,3]` as if it worked).
- [ ] Add `ArrayIndexExpr` to `KisakAstNodeKind` (children: `[0]` = base expression, `[1]` = key expression). Parse as a postfix operator in `ParsePrimary`'s chain: after parsing a primary/postfix expression, if `[` follows, parse a full expression, expect `]`, wrap into `ArrayIndexExpr` with the prior expression as base — chainable (`a[i][j]` should parse, even though nested arrays may not be exercised by the real corpus; get the grammar right regardless).
- [ ] Add `ArraySizeExpr` to `KisakAstNodeKind` (children: `[0]` = base expression; NOT `FieldAccessExpr` — see Architecture fact 4 for why keeping it separate matters). Parse: after a postfix expression, if `.` is followed by the identifier `size` AND NOT followed by `(` (i.e., not a method call — confirm this against the real corpus: `.size` is always a bare property read, never `.size()`), produce `ArraySizeExpr`; otherwise fall through to the existing `.field`/method-call parsing unchanged.
- [ ] Extend `DescribeAstNodeKind`/`DumpAst` for the 3 new node kinds (fixes the `-Wswitch` warning the same way every prior step did).
- [ ] Confirm (do not just assume) whether `ArrayIndexExpr`/`ArraySizeExpr` need to interact with the EXISTING assignment-lvalue detection in `ParseExpressionOrAssignmentStatement` at the PARSER level, or whether that detection can stay purely compiler-side (i.e., the parser always produces a plain `ArrayIndexExpr` in expression position, and the COMPILER's assignment-emission path is what special-cases "is the LHS an ArrayIndexExpr" — this mirrors how `.field` assignment detection already works, check that mechanism first and follow the same layering rather than inventing a new one).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_array_parser_test"
# Parse REAL lines from killhouse.gsc verbatim (sed -n '<N>p' the exact real
# lines first, don't paraphrase): the `lines[ lines.size ] = &"KEY";` family
# (multiple real lines use this exact idiom), `C4_models[ i ] hide();`,
# `angles[ 0 ] < -40`, `tooslow_dialog[ 0 ] = "stilltooslow";`, and a bare
# `x = [];` synthetic case. Dump the AST for each and confirm ArrayLiteralExpr/
# ArrayIndexExpr/ArraySizeExpr appear with the right shape (check against a
# hand-traced expectation, not just "it didn't crash").
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp \
  "$SCRATCH/script_array_parser_test/test_array_parser.cpp" \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  -o "$SCRATCH/script_array_parser_test/test_array_parser" && "$SCRATCH/script_array_parser_test/test_array_parser"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `[]`, `expr[key]` (including chained), and `expr.size` all parse correctly, verified against real killhouse.gsc lines, not just synthetic ones.
- All pre-existing suites still pass (parser suite especially — this step must not regress any already-parsing real corpus line). NDK build clean.

### Rollback

Purely additive to the parser (new node kinds, new parse branches in the existing postfix chain) — revert by removing the added branches.

---

## Step 3 — JOIN: array-element assignment (VM + compiler, read AND write paths)

**Depends on:** Steps 1, 2
**Model tier:** strongest

### Context brief

This is the plan's hard join point (mirrors the original VM plan's Step 8 and the namespaced-calls plan's Step 3/4) — first place VM value type, parser grammar, and compiler emission all have to agree on a real wire contract. Read Architecture fact 5 in FULL before designing anything.

**Ground truth on the existing local-variable ref/set mechanism** (confirmed by reading `kisak_script_vm_android.cpp` directly — read it yourself before starting, this is the pattern to extend, not just reference): `Frame::refSlot` (`kisak_script_vm_android.cpp:509`) is a plain `int` — the absolute index of a local variable, set by `OP_EvalLocalVariableRefCached0/Cached` (lines 796-803). The ALREADY-IMPLEMENTED `OP_SetVariableField` (lines 806-810) does exactly `frame().locals[frame().refSlot] = pop();` — it is NOT "uncommitted," and `OP_inc`/`OP_dec` already depend on this exact ref/set pair (lines 972-986). **This means `OP_SetVariableField` cannot be reused completely unchanged** — its current body only knows how to write to `frame().locals[int index]`; it needs EXTENDING (not just calling as-is) to also handle an array-element target. Concretely: extend `Frame`'s ref representation from a bare `int refSlot` to something that can represent EITHER "local slot N" OR "array element (shared_ptr<map> + KisakArrayKey)" (a small tagged variant/union is enough — do not over-engineer this into a general "reference" value type), have the new `OP_EvalArrayRef` populate the array-element case, and extend `OP_SetVariableField`'s existing body with a branch for it alongside its existing local-slot branch. This is the recommended design (matches retail's real `OP_EvalArrayRef` + generic-setter shape AND this port's own existing local-ref precedent) — only fall back to a different, single combined write opcode if this extension proves genuinely awkward once attempted, and if so it still needs a real, already-cataloged opcode ID (never invent a new numeric value).

Also confirm (read `kisak_script_compiler_android.cpp:636-640`, `IsEntityKeyword`/`kEntityDeferred`) that this is a COMPILE-TIME rejection of an assignment TARGET, not a parse-time keyword check — `level.field`/`self.field` already parse fine as an ordinary `FieldAccessExpr` today; only the compiler's assignment-target handling rejects them. This matters directly for this step: when wiring `ArrayIndexExpr` as an assignment target, the BASE expression (whatever is being subscripted) must still be emitted through the SAME entity-checked path a bareword/field assignment target already goes through — do not add a shortcut that evaluates an `ArrayIndexExpr`'s base without going through that check, or `level.x[0] = v` could silently slip past the entity-deferred rejection instead of correctly hitting it (mirroring how `self`/`level`/`game`/`anim` are already, correctly, rejected today for plain field assignment).

Read `kisak_script_compiler_android.cpp`'s existing local-variable assignment path (`OP_SetLocalVariableFieldCached0/Cached`, and how `ParseExpressionOrAssignmentStatement`/the compiler's `EmitStatement`+assignment handling currently detects "LHS is a simple identifier" and, separately, how it currently detects and rejects a field-assignment target) before designing the array-assignment equivalent.

### Tasks

- [ ] Design and implement the extended ref representation described above: `Frame`'s ref state can point at either a local slot or an array element; `OP_EvalArrayRef` populates the array-element case (pop key + array value, validate the array is genuinely `Array`-typed — non-`Array` base at runtime → `RuntimeError`, not a crash or silent no-op, matching retail's `Scr_Error` on a non-array target); `OP_SetVariableField`'s existing body gets a new branch for the array-element case, its existing local-slot branch untouched. Document the exact stack contract precisely, in the same style as `OP_ScriptFunctionCallPointer`'s stack-contract comment.
- [ ] Wire the COMPILER's assignment-emission path to recognize an `ArrayIndexExpr` as an assignment LHS (mirroring the existing simple-identifier/field-assignment detection you read above, and routing the base expression through the SAME entity-checked emission path field-assignment already uses) and emit: base-expr, key-expr, `OP_EvalArrayRef`, value-expr, `OP_SetVariableField` — no separate "eval as rvalue" path should be taken when the same `ArrayIndexExpr` is an assignment target.
- [ ] Wire the COMPILER's expression-emission path (`EmitExpression`'s switch) for the non-assignment (rvalue-read) case: `ArrayLiteralExpr` → `OP_EmptyArray`; `ArrayIndexExpr` (read context) → base-expr, key-expr, `OP_EvalArray`; `ArraySizeExpr` → base-expr, `OP_size`.
- [ ] Confirm compound assignment operators (`+=` etc.) on an array element either work correctly (establish the ref once via `OP_EvalArrayRef`, read through it, compute, write back through `OP_SetVariableField`) or are cleanly rejected with a specific compile error if not attempted this step — check whether the real corpus actually uses `arr[key] += ...`; if it doesn't, explicitly deferring compound-assignment-on-array-element with a clear error is an acceptable, documented scope cut (don't silently miscompile it).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_array_step3_test"
# Host test: compile+execute a hand-written script exercising
#   x = []; x[0] = "a"; x[1] = "b"; print(x[0], x[1], x.size);
# expect output using x[0]="a" x[1]="b" x.size=2 (or however print formats it,
# match the existing print/setdvar host-test style from step 9 of the
# original VM plan). ALSO re-run the REAL corpus lines from Step 2's parser
# test through the full compile pipeline now (they parsed then; do they
# compile+execute now?) -- C4_models[i]/aa[i]/angles[0]/tooslow_dialog[i]
# are all real, plain-variable, now potentially fully working constructs;
# confirm precisely which do and don't (some may still hit an UNRELATED
# gap further down the same function, document honestly, same as every
# prior real-corpus check in this project).
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_array_step3_test/test_step3.cpp" \
  android/app/src/main/cpp/kisak_script_compiler_android.cpp \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  android/app/src/main/cpp/kisak_script_entity_android.cpp \
  "$SCRATCH/script_compiler_test/dvar_host_stub.cpp" \
  -o "$SCRATCH/script_array_step3_test/test_step3" && "$SCRATCH/script_array_step3_test/test_step3"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Array literal, subscript read, subscript write (including overwrite of an existing key), and `.size` all compile and execute correctly end to end, host-verified.
- The opcode-reuse decision is documented as explicitly as every other scope decision in this plan.
- All pre-existing suites pass. NDK build clean on 4 ABIs.

### Rollback

If the chosen opcode-reuse design proves wrong during Step 4's real-corpus validation, the write-path opcode(s) and compiler emission for it can be reworked in isolation — Steps 1/2's read-only value type and grammar stay valid regardless of how the write path is ultimately wired.

---

## Step 4 — Real-corpus validation + hand-written array test

**Depends on:** Step 3
**Model tier:** default

### Context brief

Same role as the original VM plan's Step 9/10 and the namespaced-calls plan's Step 6, but split out as its own step here since there's substantive new validation work (re-running 3 real levels + a dedicated reference-semantics test) before device time is worth spending. Do this on host first.

### Tasks

- [ ] Re-run the SAME 3 real levels (`killhouse` via the real device-extracted `.gsc` already on disk, `bog_a`/`cargoship` via their pulled `.ff` files, same fastfile/rawfile/compiler host pipeline established in `scratchpad/step10_multilevel/`) through the full pipeline. **Confirm the failure LINE stays the same, but the failure CATEGORY changes** — this is the honest, precise expectation, verified against the CURRENT (pre-this-plan) behavior: as of this plan being drafted, all three currently fail with a PARSE error at their `level.*` array-literal line (killhouse:204/bog_a:71: `parse: ... unexpected operator '[' in expression`; cargoship:10: `parse: ... expected ';', got '['`) — the array grammar doesn't exist yet, so parsing dies before the entity-deferred compiler check is ever reached. AFTER this plan (Steps 1-3 land), `[]`/`[key]` parse successfully, so these same lines should now reach the COMPILER and fail there instead, with the pre-existing entity-deferred rejection message (`kisak_script_compiler_android.cpp`'s `kEntityDeferred` text) on the `level.<field>` assignment target. Confirm this category change (parse error → compile error, same line) precisely for all three; if any level's failure line ITSELF moves (not just the error category), figure out why before reporting success — that would be a genuine surprise worth investigating, not silently accepting.
- [ ] Write and run a dedicated array reference-semantics test (a function receiving an array parameter, mutating an element, caller observes the mutation) — this is the single most important behavioral property of this blueprint per Architecture fact 2, and hasn't been exercised end-to-end (only unit-level in Step 1) until now.
- [ ] Extract and compile the REAL plain-variable-array lines identified in this plan's Objective (`C4_models[i] hide();`, `aa[i] hide()/notsolid()`, `lines[lines.size] = &"KEY";`, `angles[0]`, `tooslow_dialog[0..3]`, `targets[selected_target] thread moveTargetDummy(...)`) as isolated snippets (stubbing any unrelated builtin/entity dependency the same way the namespaced-calls plan's Step 3 isolated `::inside_start` from `getent`/`level.player`) to prove the array CONSTRUCT itself works against real syntax, even though the full functions they're drawn from won't compile end-to-end yet (they use `hide()`/`notsolid()` method-call syntax on entities, `thread`, `waittill` — all separately deferred gaps, unrelated to arrays).
- [ ] Update `plans/gscript-real-source-notes.md` with this step's findings (matching the addendum pattern every prior step used).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
bash /tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad/run_all_tests.sh
# plus the new array-specific suites from steps 1-3, plus the new
# reference-semantics + real-corpus-snippet tests from this step
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- killhouse/bog_a/cargoship's failure LINES confirmed unchanged, but their failure CATEGORY confirmed to move from a parse error to the pre-existing compile-time entity-deferred rejection — an explicit, honest check of real, positive (if partial) progress, not silence and not overselling.
- Reference-semantics test passes.
- At least the isolated real-corpus array snippets listed above compile and execute correctly.
- `plans/gscript-real-source-notes.md` updated.

### Rollback

Validation-only step; a failure here points back to Steps 1-3, not this step itself.

---

## Step 5 — Device validation + gap-list + memory update

**Depends on:** Step 4
**Model tier:** default

### Context brief

Same role as the namespaced-calls plan's Step 6 and the original VM plan's Step 10 — confirm on real device hardware, update the shared gap-tracking doc, update auto-memory. No new subsystem code.

### Tasks

- [ ] Run the full pipeline on real device (`New Game` -> `devmap killhouse`, same navigation established across this entire session) and confirm: no crash, killhouse's failure LINE is unchanged (still 204) but the failure CATEGORY matches Step 4's host-confirmed result — the parse error is gone, replaced by the pre-existing `level.weaponClipModels` entity-deferred COMPILE rejection (this blueprint was never going to get PAST line 204, but it should genuinely reach and correctly reject it now, rather than fail earlier during parsing) — confirm that expectation held on real device too, not just host.
- [ ] Confirm no regressions in the world/gameplay milestones already validated this session (world render, static props including the vehicle/prop texture fix from earlier this session, viewmodel weapon, HUD move-stick; hitscan fire/audio synthetic-input reproduction has been flaky before — if it's flaky again, document honestly as inconclusive rather than claiming a false pass, matching this project's own established standard).
- [ ] Update `plans/gscript-vm-gaps.md`: mark plain-variable arrays as covered (with the explicit level/self-field carve-out noted), update the "Not covered" table's array row, and re-rank the recommendation list (the entity/object-field-access blueprint becomes the clear next highest-priority item, since it's now the ONLY thing standing between the real corpus and further progress on killhouse/bog_a/cargoship's own next lines).
- [ ] Update `/home/jacques/.claude/projects/-home-jacques/memory/project_kisakcod_android.md` following the exact structure every prior milestone entry in that file used (French, bold header, what/how/validated, gotchas).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "step9\|array\|zone de mission"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Device pass confirms no crash, no regressions, and the expected failure-category change (parse error → compile-time entity-deferred rejection) at the unchanged killhouse line 204.
- `plans/gscript-vm-gaps.md` and auto-memory updated with concrete findings, matching every prior step's documentation quality bar.

### Rollback

Documentation-only step; a regression found here is a bug in Steps 1-4, filed against the responsible step.

---

## Plan-level notes for whoever executes this

- **Do not attempt `level`/`self` field-array access, entity field access in general, `switch`, or threading** even if a real script's behavior after this plan makes one of them look tractable — those remain separate blueprints, per `gscript-vm-gaps.md`'s own recommended order (this plan first, then entity/object-field-access, which arrays are now a direct prerequisite for).
- **Do not attempt populated array literals (`[1,2,3]`)** — confirmed not to exist anywhere in the real corpus and not backed by any real opcode; `OP_EmptyArray` is genuinely the only literal form.
- **Do not attempt `foreach`-style iteration** — not in this port's grammar today, out of scope, and per Step 1's own task notes, its absence is exactly why array key ordering is currently unobservable/inconsequential.
- Every step reuses the established host-then-device workflow: prototype in `/tmp/claude-*/scratchpad` with `g++` before touching the NDK build; re-run `run_all_tests.sh` after every step.
- If the prior blueprints' scratchpad `.gsc`/`.ff` files didn't survive a session boundary, re-derive them exactly as documented in "What already exists" above — every real-corpus claim in this plan depends on genuine source text, not paraphrase.
