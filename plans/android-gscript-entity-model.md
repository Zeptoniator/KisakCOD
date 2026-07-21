# GScript entity/object model (self/level/game, field access, object-prefixed calls) — blueprint

Direct mode (no branches/PRs — matches all four prior GScript blueprints). Fork remote `fork` = `github.com/Zeptoniator/KisakCOD.git` (writable); `origin` = upstream, read-only.

## Objective

Extend the GScript VM port with a real **self/level/game object model**: generic field read/write on these objects, and **object-prefixed calls** (`<expr> thread funcName(...);` and `<expr> funcName(...);`, both rebinding the callee's `self`) — the confirmed, measured top-priority remaining gap per `plans/gscript-vm-gaps.md` (re-ranked 2026-07-21 after the arrays and threading blueprints both shipped).

**Real corpus evidence, confirmed by direct extraction (not assumed):**
- `killhouse.gsc:221` and `cargoship.gsc:172` **both** fail whole-file parsing at the *identical* construct: `level thread maps\<file>::main();` — a receiver-prefixed (`level`), namespaced, threaded call.
- Field access on `level`/`self` is **not a rare, edge-case construct** — it is by far the dominant real-corpus pattern this session has found: `level.player` (126×), `level.waters` (44×), `level.heroes5` (191×), `level.heroes3` (135×), `level.heli` (80×) in killhouse/cargoship alone; field *writes* (`level.x = ...` / `self.x = ...`) occur 20/138/58 times across killhouse/cargoship/bog_a respectively — including as early as `killhouse.gsc:9` (`level.short_training = true;`) and `cargoship_extract.gsc:10` (`level.fogvalue["near"] = 100;`, a field that is itself an **array**, and — critically — this is the *first statement of cargoship's `main()`*, with no prior `level.fogvalue = [];` anywhere. Verified by adversarial review: this is an **auto-vivification** case (GSC creates the array on first indexed write to an unset field), which this port's existing array machinery does **not** do for free — see the Scope Cut and Step 4 for the explicit decision this requires).
- A non-threaded receiver-prefixed call to a real user function also occurs: `bog_a_extract.gsc:807`: `self set_force_color("c");` — same self-rebinding mechanism as the threaded form, minus the `thread` keyword.
- `level`/`self` immediately followed by `waittill(...)`/`notify(...)`/`endon(...)` is **also extremely common** (`level notify(...)`, `level waittill(...)`, `level endon(...)`, `self endon(...)` — dozens of real sites per file) and looks, superficially, exactly like the in-scope receiver-prefixed-call grammar this plan adds. These are explicitly OUT of scope (see Scope Cut) and **must** still produce a clean, specific error — not silently miscompile as a call to a nonexistent user function named `waittill`/`notify`/`endon`. Disambiguating this correctly is one of this plan's real risks, not a footnote.

**A genuine surprise, found during this plan's own research (not assumed going in), worth stating precisely because it changes how urgent this work actually is:** `CompileGscSource` parses a file's *entire* `Program` before compiling *any* of it (established pattern from every prior blueprint this session). `level thread maps\killhouse_amb::main();` at line 221 is a **parse-time** failure (`expected ';', got 'thread'`) — meaning the compile phase, where `kEntityDeferred` would reject `level.short_training = true;` at line 9, **never even starts** today. Fixing *only* the object-prefixed-thread parser gap (without also fixing field-access compilation) would not usefully advance killhouse or cargoship at all — the very next thing the compiler would hit, the moment the file finishes parsing, is the field write at line 9, still rejected by `kEntityDeferred`. Field-access codegen and object-prefixed-call codegen are therefore **not two independently-useful features that happen to be bundled for convenience** — the file cannot compile past its own first few lines without both. This is confirmed by direct research, not an assumption carried over from the objective this plan was drafted against.

## Real retail architecture (confirmed by direct research in `src/script/scr_vm.cpp`, not guessed)

- `self`/`level`/`game`/`anim` are **all the same kind of value**: a `VAR_POINTER` wrapping an `objectId` into a shared, generic variable-pool object. `OP_GetSelf`/`OP_GetLevel`/`OP_GetGame`/`OP_GetAnim` (`scr_vm.cpp:2273-2297`) all just push a pointer to one of these objects (`scrVarPub.levelId`/`gameId`/`animId` are persistent singleton ids; self is per-thread, via `Scr_GetSelf(fs.localId)`).
- Field access is **one generic mechanism** regardless of which object it's on: `Scr_FindVariableField(objectId, fieldNameStringTableId)` / `FindVariable` (`OP_EvalSelfFieldVariable`/`OP_EvalLevelFieldVariable`/`OP_EvalFieldVariable`, `scr_vm.cpp:2432-2458`) — a per-object string-keyed map. Retail has several *fast-path* opcodes for the well-known objects (`OP_EvalLevelFieldVariable` etc.) plus one fully generic one (`OP_EvalFieldVariable`, operating on whatever `objectId` a prior instruction computed) — **this port does not need the fast-path duplication**; collapsing to one generic mechanism (matching this port's own established simplification precedent: `OP_EvalArray` collapsed several of retail's array fast-paths into one opcode in the arrays blueprint) is the right call here too. **Confirmed by adversarial review: this port's own `KisakScriptOpcode` enum already declares the correctly-named generic opcodes for exactly this — `OP_EvalFieldVariable` (`0x2A`) and `OP_EvalFieldVariableRef` (`0x2E`), both currently unimplemented, alongside the unused retail fast-path siblings (`0x27`/`0x29`/`0x2B`/`0x2D`). Implement against these two existing enum entries — do not invent new opcode names/IDs (see Architecture fact 4/5 below, corrected from this plan's own earlier draft).**
- Field *write* (`OP_SetVariableField`/`OP_SetSelfFieldVariableField`, `scr_vm.cpp:2564-2648`) parallels the arrays blueprint's already-implemented `RefKind::ArrayElement` `Frame`-ref mechanism (`kisak_script_vm_android.cpp:547-556`) closely enough that extending the *same* tagged `RefKind` with a new `ObjectField` variant — reusing `OP_SetVariableField`'s existing dispatch rather than inventing a new opcode — is the natural design (matches the arrays review's own precedent: reuse existing setters, don't invent new opcodes without cause).
- Method-call dispatch (`OP_CallBuiltinMethod`, `scr_vm.cpp:2648+`) requires `GetObjectType(objectId) == VAR_ENTITY` and dispatches into a real builtin entity-method table (`entity.hide()`-style). This port has **no game-world/entity-simulation backing** for that at all, and no real corpus blocker found this session needs it — **firmly out of scope**.
- Object-prefixed calls (`<expr> thread funcName(...);` and `<expr> funcName(...);`) are a **different, simpler** mechanism than builtin methods: retail has dedicated opcodes for exactly this — `OP_ScriptMethodCall` (`0x52`) and `OP_ScriptMethodThreadCall` (`0x56`), confirmed already present (declared, unimplemented) in this port's own fixed 139-entry `KisakScriptOpcode` enum (`kisak_script_vm_android.h:111,115`) alongside `OP_ScriptMethodCallPointer`/`OP_ScriptMethodThreadCallPointer` (`0x53`/`0x57`, both out of scope — the local-`FunctionRef`-pointer tier, same cut the threading blueprint already made for the non-method call forms). These pop an evaluated receiver value and use it as the new frame's `self`, instead of inheriting the caller's `self` — the *only* real difference from `OP_ScriptFunctionCall`/`OP_ScriptThreadCall`.
- `self`/`level`/`game`/`anim` are **lexer keywords** (`kisak_script_lexer_android.h:82`, not plain identifiers), which `ParsePrimary` converts to a plain `IdentifierExpr` carrying the literal keyword text (documented in-code, `kisak_script_compiler_android.cpp:65-70`, parser conversion confirmed at `kisak_script_parser_android.cpp:611-617` via `IsObjectKeyword`). **This keyword status is exactly what makes them safe to disambiguate from `waittill`/`notify`/`endon`** (also keywords — see below): a keyword can never be misparsed as a call to a user-defined function, since the call-expression grammar requires an `Identifier`-typed token, not a keyword. And **ordinary field access on any expression** (`level.foo`, `self.foo`) already parses today as a perfectly normal `FieldAccessExpr` — the postfix `.` grammar has never cared what the base expression's name is.
- **Confirmed by adversarial review: the non-threaded object-prefixed call form ALSO already parses today**, with no new grammar — `parser.cpp:337-348`'s existing `MethodCallExpr` production fires whenever `Peek().type == Identifier && Peek(1) == "("` follows a parsed primary expression, so `self set_force_color("c");` already produces a `MethodCallExpr{text: "set_force_color", object: self, args: [...]}` today. It is currently rejected only at the **compiler** stage (`kisak_script_compiler_android.cpp:336`, via `kEntityDeferred`), not the parser. The **only** genuinely new parser grammar this plan needs is for the **threaded** object-prefixed call form (`<expr> thread <call>;`) — `thread` is a keyword token, so it can never satisfy today's `MethodCallExpr` lookahead, which is why killhouse:221/cargoship:172 fail to parse while bog_a:807 already parses fine. The compiler-side work is replacing `IsEntityKeyword()`/`kEntityDeferred`'s rejection call sites (grep-confirm this list fresh — see Step 4's corrected per-site mapping, since two of them are guards that must stay rejected, not become codegen) with real codegen for the sites that ARE genuinely supported AST shapes.

## Scope cut (read before doing anything else)

**In scope:**
1. `self`/`level`/`game` as generic field-storage objects — read (`x = level.foo;`) and write (`level.foo = x;`). Write to an **already-populated array-typed field** (`level.foo[key] = x;` where `level.foo` was previously assigned a real `Array` value, e.g. via `level.foo = [];`) composes for free through the existing array machinery once field-read produces a real `Array` value — confirm in Step 5. Write to an **unset field via array-index syntax with no prior `= []`** (the corpus's own `cargoship_extract.gsc:10`: `level.fogvalue["near"] = 100;`, first statement of `main()`, no preceding init) is a **separate, explicitly in-scope task**: real GSC auto-vivifies the array on first indexed write; this port must too, or the corpus's own headline example does not execute. See Step 4 Task list for the explicit auto-vivification codegen this requires — it is not free, and Step 4/5's exit criteria are written to require it.
2. Object-prefixed calls: `<expr> thread funcName(...);` / `<expr> thread path\file::func(...);` (rebind self + thread) and `<expr> funcName(...);` / `<expr> path\file::func(...);` (rebind self, not threaded) — both confirmed real (killhouse/cargoship's shared blocker; bog_a's `self set_force_color("c");`).
3. `self` inheritance through ordinary (non-rebinding) calls: a callee invoked via a normal `OP_ScriptFunctionCall`/`OP_ScriptThreadCall` inherits the **caller's current `self`** (real retail semantics) — needed for `self.field` to mean anything sensible inside any nested call.

**Out of scope, explicit and defensible (matching every prior blueprint's own scope-cut precedent):**
1. **`anim`/`animtree`/`usingtree`** — present in the lexer, unused by the confirmed real corpus at every current failure boundary across 4 blueprints' worth of re-validation this session. Deferred until a real script actually needs one.
2. **Entity builtin-method calls** (`OP_CallBuiltinMethod`, `entity.hide()`-style) — this port has no real game/entity-simulation system to dispatch into, and nothing in the corpus needs it. `spawn()` returning a real, VM-visible entity/object value is *also* out of scope for the same reason — the existing `KisakScriptEntity`/`SpawnEntitiesFromMapEntsString` module (`kisak_script_entity_android.h/.cpp`) is a standalone map-ents **text parser** from an earlier VM-port step, not wired into the script VM's value system at all; wiring it in is a separate future concern.
3. **`waittill`/`notify`/`endon`** — real thread-suspension/signaling semantics this port's synchronous-inline VM has no scheduler for (the same architectural gap the threading blueprint already identified and cut for bare `thread`). These superficially resemble this plan's in-scope object-prefixed-call grammar (`level notify(...)` looks like `level funcName(...)`) — **but adversarial review confirmed the "silent miscompile as a call to a nonexistent user function" scenario this plan originally worried about cannot actually happen**: `waittill`/`waittillmatch`/`waittillframeend`/`notify`/`endon` are lexer **keywords** (`kisak_script_lexer_android.h`, not identifiers), and the `MethodCallExpr`/object-prefixed-call grammar (both the already-parsing non-threaded form and Step 3's new threaded form) requires an `Identifier`-typed token for the call name — a keyword token can never satisfy that, so `level notify(...)` already fails to parse cleanly today (`expected ';', got 'notify'`) and will continue to after this plan, with no extra work required to prevent miscompilation. The remaining, real (but much lower-stakes) task is **polish, not safety**: upgrade the generic parse-error message to a specific "waittill/notify/endon are a deferred subsystem" message at the point Step 3's grammar would otherwise reject them, so the error is legible rather than a raw token-mismatch. See Step 3/Plan-level-notes, reframed accordingly.
4. **Assignment to a bare object keyword** (`level = x;`, `self = x;`, rejected today at `kisak_script_compiler_android.cpp:761`) — no real corpus usage found this session; keep rejected. Reassigning the whole `level`/`self`/`game` binding has no sensible real-retail semantics this port needs to support.
5. **Real concurrency for the threaded receiver form** — `OP_ScriptMethodThreadCall` reuses the exact same synchronous-inline simplification the threading blueprint already documented and justified for `OP_ScriptThreadCall` (runs to completion immediately, return value discarded via a trailing `OP_DecTop`, no real parallelism). Not re-litigated here.
6. **Cross-`Execute()`-call persistence of `level`/`game`** — this port constructs a fresh `Interpreter` per `Execute()` call, and (per the current renderer wiring) performs exactly one `Execute()` call per zone/script trigger today. Real retail's `level`/`game` persist across an entire game session, spanning many separate script-thread invocations; this port's `level`/`game` are freshly allocated at the start of each `Execute()` call. This distinction is currently unobservable (nothing calls `Execute()` twice against the same loaded zone yet) — document it as a known, honest simplification, do not attempt to build session-spanning persistence now.

## Architecture facts locked in by research (2026-07-21)

1. **New `Object` value type**: `shared_ptr<map<string, KisakScriptValue>>` — same reference-semantics shape as the existing `Array` value type (`KisakScriptValueType::Array`), with string-only keys (field names) instead of `KisakArrayKey`. Reusing the array's proven shape (rather than inventing a new container) is deliberate — it is the same "generic variable-pool object" retail itself uses for both arrays and self/level/game/entities under the hood.
2. **Frame gains a `self` member** (a `KisakScriptValue`, defaulting to `Undefined` at the outermost call frame — this port has no top-level "world entity" to bind it to, an honest simplification, not a retail-faithful default). Ordinary calls (`OP_ScriptFunctionCall`/`OP_ScriptThreadCall`, **and `OP_ScriptFunctionCallPointer`** — confirmed by adversarial review this pointer-call path also pushes a frame and must inherit `self` too, or `self` silently drops to `Undefined` through any `[[fp]]()` call) copy the **caller's current `self`** into the new frame (inheritance). `OP_ScriptMethodCall`/`OP_ScriptMethodThreadCall` instead pop an explicit receiver value off the stack (pushed by the compiler immediately before the call, matching retail's real calling convention) and use *that* as the new frame's `self`.
3. **`level`/`game` are singleton `Object` values owned by the `Interpreter`**, lazily or eagerly allocated once at the start of `Execute()` and handed out by value (the `KisakScriptValue` wrapping them is copied, but the underlying `shared_ptr<map<...>>` is shared — exactly the arrays' existing reference-semantics pattern) every time `OP_GetLevel`/`OP_GetGame` runs. `anim` is out of scope (see Scope Cut).
4. **Field read**: **use the existing declared-but-unimplemented `OP_EvalFieldVariable` (`0x2A`) enum entry — do not invent a new opcode name/ID** (corrected by adversarial review: an earlier draft of this plan proposed a fresh `OP_EvalObjectField`, which would have violated this port's own fixed-139-entry-enum / no-new-opcode-IDs constraint honored by every prior blueprint; the enum already has the right generic entry, plus unused retail fast-path siblings `0x27`/`0x29`/`0x2B`/`0x2D` to leave unimplemented, matching how `OP_EvalArray` collapsed retail's array fast-paths). `OP_EvalFieldVariable` pops an object-valued expression result, reads a field-name operand encoded as a `stringPool` index — **matching how `OP_GetString` encodes its operand** (`InternString` + `EmitU16` at compile time, `U16` read + string-pool lookup at runtime; corrected by adversarial review — the earlier draft's guidance to "mirror `OP_EvalArrayRef`'s key operand encoding" was wrong, since `OP_EvalArrayRef` has no encoded operand at all, it *pops* a runtime-computed key value; a field name is a compile-time constant, so the `OP_GetString` precedent is the correct one) — then pushes the field's current value (or `Undefined` if the field has never been written — matching this port's own already-established missing-key-returns-Undefined precedent from the arrays blueprint, and matching real retail's own behavior for an unset field).
5. **Field write**: extend `RefKind` (`kisak_script_vm_android.cpp:547`) with `ObjectField` (a port-internal enum variant name, not an opcode — parallel to the existing `ArrayElement` variant: `refObject` — a `shared_ptr` copy of the target object's field map — plus `refFieldName`). Established via the existing declared-but-unimplemented **`OP_EvalFieldVariableRef` (`0x2E`)** opcode (mirrors `OP_EvalArrayRef`'s shape; same corrected reuse-don't-invent guidance as fact 4 above), consumed by the *already-implemented* `OP_SetVariableField` dispatch (extend its `switch (refKind)` with the new case) — no new setter opcode, matching the arrays review's own MEDIUM-5 precedent.
6. **`OP_ScriptMethodCall`/`OP_ScriptMethodThreadCall`**: confirmed already present (declared, unimplemented) in the fixed 139-entry `KisakScriptOpcode` enum (`kisak_script_vm_android.h:111,115`) — reuse them, do not invent new opcode IDs (this port's own established constraint, honored by every opcode implemented across all four prior blueprints). Dispatch is otherwise identical to `OP_ScriptFunctionCall`/`OP_ScriptThreadCall` (argument binding, frame push, synchronous-inline thread simplification) with the one addition: pop the receiver value pushed by the compiler and use it as the new frame's `self` instead of inheriting the caller's.
7. **Parser**: no new grammar for plain field access (already parses as `FieldAccessExpr`) or for bare `self`/`level`/`game` references (already parse as `IdentifierExpr`). **Corrected by adversarial review**: the **non-threaded** object-prefixed call form also needs no new grammar — `parser.cpp:337-348`'s existing `MethodCallExpr` production already fires for `<expr> <identifier>(...)` (verified: `self set_force_color("c");` already parses as `MethodCallExpr{text:"set_force_color", object:self, args:[...]}` today, rejected only at the compiler stage). New grammar is needed *only* for the **threaded** form (`<expr> thread <call>;`), since `thread` is a keyword token that the existing `MethodCallExpr` lookahead never expects. This new threaded-form grammar must be disambiguated from an ordinary expression-statement (e.g. `level.foo = 1;` is NOT an object-prefixed call) — the out-of-scope `waittill`/`notify`/`endon` forms need **no active disambiguation work**, since they are keywords, not identifiers, and structurally cannot satisfy either the existing `MethodCallExpr` lookahead or the new threaded-form lookahead (see Scope Cut item 3 for why the earlier draft's "sharpest risk" framing here was overstated). Confirm the exact threaded-form lookahead strategy against the real corpus in Step 3.

## De-risking strategy

Same shape as every prior GScript blueprint: VM opcode semantics first (hand-assembled bytecode, no parser/compiler needed), split into two VM steps given this subsystem's larger size (field access, then method-call opcodes), parser grammar next, then a compiler join point, then real-corpus validation, then device.

## Step graph

```
Step 1 (VM: Object value type, Frame.self + inheritance, OP_GetSelf/Level/Game,
         generic field read/write opcodes incl. RefKind::ObjectField)
                    |
                    v
Step 2 (VM: OP_ScriptMethodCall / OP_ScriptMethodThreadCall — receiver rebinds self)
                    |
Step 3 (Parser: object-prefixed call statement grammar, disambiguated from plain   } run in
         expression-statements and from waittill/notify/endon)                    } parallel
                    |                                                             } with Step 2
                    v
Step 4 (JOIN: compiler wiring — replace all 6 kEntityDeferred call sites with real codegen)
                    |
                    v
Step 5 (Real-corpus validation: killhouse/cargoship/bog_a re-run, confirm the field-access
         cascade unblocks past line 9, confirm the shared `level thread` blocker resolves,
         confirm waittill/notify/endon still cleanly rejected, confirm level.field[key] works)
                    |
                    v
Step 6 (Device validation + gap-list + memory update)
```

---

## Step 1 — VM: `Object` value type, `Frame.self`, self/level/game, generic field read/write

**Depends on:** nothing
**Model tier:** strongest (this is the plan's central design decision — the value-type shape, the ref mechanism extension, and the self-inheritance default all need to be right before anything else builds on them)

### Context brief

Read Architecture facts 1-5 in full. Read the existing `Array` value type end to end (`kisak_script_vm_android.h`'s `KisakScriptValueType`/`KisakScriptValue`, and the arrays blueprint's `RefKind::ArrayElement`/`OP_EvalArrayRef`/`OP_SetVariableField` in `kisak_script_vm_android.cpp`) — this step's `Object` type and `RefKind::ObjectField` are deliberately structured as the field-name-keyed sibling of that exact mechanism, not a fresh design. Read `Frame`'s current definition (`kisak_script_vm_android.cpp:548-556`) and the `OP_ScriptFunctionCall`/`OP_ScriptThreadCall` dispatch (frame-push mechanics) before adding the `self` member and its inheritance copy.

### Tasks

- [ ] Add `KisakScriptValueType::Object` (`kisak_script_vm_android.h`), backed by `std::shared_ptr<std::map<std::string, KisakScriptValue>>` — document the reference-semantics rationale (same as `Array`) and that keys are plain field names, not `KisakArrayKey`.
- [ ] Add a `self` member to `Frame` (`KisakScriptValue`, default `Undefined`). At `OP_ScriptFunctionCall`/`OP_ScriptThreadCall`/**`OP_ScriptFunctionCallPointer`**'s frame-push (all three push a frame; per Architecture fact 2, the pointer-call path must inherit `self` too or it silently drops to `Undefined`), copy `frames.back().self` (the caller's current self) into the new frame — document this as real-retail-faithful inheritance, and that the outermost frame's `Undefined` default is this port's own honest simplification (no top-level "world entity" exists here).
- [ ] Add `level`/`game` singleton `Object` values to `Interpreter` (allocated once at the start of `Execute()`). Implement `OP_GetSelf` (push `frames.back().self`), `OP_GetLevel`, `OP_GetGame` (push the singleton, sharing the underlying map via `shared_ptr` copy — confirm this matches the arrays blueprint's own aliasing test pattern before considering it done).
- [ ] Implement generic field read using the **existing declared-unimplemented `OP_EvalFieldVariable` (`0x2A`)** enum entry (do not add a new opcode — see Architecture fact 4): pop an object-valued operand, read a field-name operand encoded as a `stringPool` index exactly like `OP_GetString` (`InternString`/`EmitU16` at compile time — this is compiler-side work landing in Step 4, but the VM-side U16-read-and-lookup half belongs here), push the field's value or `Undefined` if unset. `RuntimeError` if the popped operand is not `Object`-typed (matching retail's `"%s is not an object"` honesty).
- [ ] Extend `RefKind` (`kisak_script_vm_android.cpp:547`) with `ObjectField` (a port-internal enum variant, not an opcode; fields: a `shared_ptr` copy of the target object's field map, a field-name string). Implement using the **existing declared-unimplemented `OP_EvalFieldVariableRef` (`0x2E`)** enum entry (mirrors `OP_EvalArrayRef`'s shape; do not add a new opcode) to establish this ref. Extend `OP_SetVariableField`'s existing `switch (refKind)` with the new case — do not add a new setter opcode.
- [ ] Extend `DescribeAstNodeKind`/opcode-name debug helpers for anything new added here (fixes `-Wswitch`, matching every prior step's pattern).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_entity_vm_test"
# Hand-assembled bytecode tests (script_vm_test/kisak_script_asm_test.h pattern):
#   - OP_GetLevel twice in the SAME Execute() call, write a field through one
#     reference, read it back through the OTHER -- proves shared_ptr aliasing
#     (the arrays blueprint's own load-bearing aliasing property, now on objects)
#   - OP_GetSelf at the outermost frame reads Undefined (documented default)
#   - a nested OP_ScriptFunctionCall's frame inherits the caller's self
#     (Step 2's method-call opcode does not exist yet at this point in the
#     plan -- seed frames.back().self directly in the test harness before
#     dispatching OP_ScriptFunctionCall, then confirm OP_GetSelf inside the
#     callee returns that SAME seeded value; corrected by adversarial review,
#     which caught the earlier draft's circular dependency on Step 2 here)
#   - reading a never-written field returns Undefined, not a RuntimeError
#   - OP_EvalFieldVariable/OP_EvalFieldVariableRef against a non-Object operand
#     -> RuntimeError with a clear message
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_entity_vm_test/test_entity_vm.cpp" \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  -o "$SCRATCH/script_entity_vm_test/test_entity_vm" && "$SCRATCH/script_entity_vm_test/test_entity_vm"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `Object` value type, `Frame.self` + inheritance, `level`/`game` singletons, generic field read/write (including the `RefKind::ObjectField` ref mechanism) all implemented and host-tested, including the aliasing and self-inheritance properties specifically.
- All pre-existing host suites pass. NDK build clean on 4 ABIs.

### Rollback

Purely additive to `kisak_script_vm_android.h`/`.cpp` (new value type, new `Frame` member, new opcodes, new `RefKind` variant) — revert by removing them; nothing else depends on this step yet.

---

## Step 2 — VM: `OP_ScriptMethodCall` / `OP_ScriptMethodThreadCall` (receiver rebinds self)

**Depends on:** Step 1
**Model tier:** default

### Context brief

Read Architecture fact 6. This is a small, mechanical step once Step 1's `self`-inheritance default exists: these two opcodes are byte-identical to `OP_ScriptFunctionCall`/`OP_ScriptThreadCall` except for one thing — instead of copying the caller's `self` into the new frame, they pop an explicit receiver value (pushed by the compiler immediately before the call, in Step 4) and use *that*. `OP_ScriptMethodThreadCall` reuses the exact synchronous-inline + trailing-`OP_DecTop`-discard simplification the threading blueprint already built for `OP_ScriptThreadCall` — do not re-derive that design, just apply it here too.

### Tasks

- [ ] Implement `OP_ScriptMethodCall`: identical to `OP_ScriptFunctionCall` except the new frame's `self` is the popped receiver value (type-check: `RuntimeError` if the receiver isn't `Object`-typed, matching retail's `"is not an entity"` honesty — confirm whether real retail also accepts non-object receivers for this opcode specifically, or only for `OP_CallBuiltinMethod`; document whichever is found).
- [ ] Implement `OP_ScriptMethodThreadCall`: identical to `OP_ScriptThreadCall` (synchronous-inline, return value left for the compiler's trailing `OP_DecTop` to discard) except for the same receiver-as-self substitution.
- [ ] Do **not** implement `OP_ScriptMethodCallPointer`/`OP_ScriptMethodThreadCallPointer` — out of scope (local-`FunctionRef`-pointer tier), matching the non-method call forms' existing cut. Leave logged as unsupported with a specific error, not silent misdispatch.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_entity_method_test"
# Hand-assembled bytecode: call a function via OP_ScriptMethodCall with an
# explicit receiver object, confirm OP_GetSelf inside the callee returns
# THAT receiver (not the caller's self); same for OP_ScriptMethodThreadCall,
# plus confirm its return value is correctly left for a discard, matching
# the threading blueprint's own OP_ScriptThreadCall stack-depth test pattern.
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_entity_method_test/test_method_vm.cpp" \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  -o "$SCRATCH/script_entity_method_test/test_method_vm" && "$SCRATCH/script_entity_method_test/test_method_vm"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Both opcodes correctly rebind `self` to the popped receiver and otherwise behave identically to their non-method counterparts.
- All pre-existing suites pass. NDK build clean.

### Rollback

Purely additive — revert by removing the two new dispatch cases.

---

## Step 3 — Parser: object-prefixed call statement grammar

**Depends on:** nothing (parallel-eligible with Step 2)
**Model tier:** strongest (getting the new threaded-form grammar to compose correctly with the ALREADY-parsing non-threaded `MethodCallExpr` form, without regressing it, is the real risk — not disambiguation against `waittill`/`notify`/`endon`, which adversarial review confirmed is structurally impossible to get wrong; see below)

### Context brief

Read Architecture fact 7 (corrected) and Scope Cut item 3 (corrected) in full before starting — **an earlier draft of this plan significantly overstated the risk here; adversarial review corrected it.** No new grammar is needed for plain field access, bare `self`/`level`/`game` references, **or the non-threaded object-prefixed call form** — confirm this by testing that `level.foo`, `level.foo = 1;`, `x = self.bar;`, AND `self set_force_color("c");` all already parse today (the last one as an existing `MethodCallExpr`, `parser.cpp:337-348`) before writing any new production. The **only** new grammar this step adds is for the **threaded** form: a statement starting with an expression, followed by the `thread` keyword, followed by a call (bare or namespaced). Real corpus forms to support: `level thread maps\killhouse_amb::main();` (killhouse:221, cargoship:172). This new threaded-form grammar must be disambiguated from an ordinary expression-statement (e.g. `level.foo = 1;` is not a threaded call). It does **not** need active disambiguation against `waittill`/`notify`/`endon`/`waittillmatch`/`waittillframeend`: these are lexer keywords, not identifiers, and the call-grammar (both the pre-existing non-threaded `MethodCallExpr` production and this step's new threaded production) requires an `Identifier`-typed token for the call name — a keyword token structurally cannot satisfy that, so `level notify(...)`/`level waittill(...)`/`self endon(...)` already fail to parse today (`expected ';', got 'notify'` etc.) and require no new logic to keep failing. The one real (low-risk, polish-only) task is upgrading that generic mismatch error to a specific "waittill/notify/endon: deferred subsystem" message when the offending keyword is recognizable at the failure point.

### Tasks

- [ ] Add a single new node kind for the threaded object-prefixed call statement only (e.g. `MethodThreadCallStatement`: children = receiver expression, call expression — reuse `CallExpr`/`NamespacedCallExpr` unchanged) to `KisakAstNodeKind`. Do **not** add a parallel node for the non-threaded form — that continues to use the existing `MethodCallExpr` (`parser.cpp:337-348`) unchanged; Step 4 wires codegen for both `MethodCallExpr` and this new node.
- [ ] At statement level, after parsing a leading expression, check whether the next token is the `thread` keyword; if so, consume it and require a call-shaped expression (identifier or namespaced-path immediately followed by `(`) to follow, producing the new threaded node. Document the exact lookahead used.
- [ ] Add a specific, legible parse/compile error (your choice which phase — document it) for the case where the call-target position is one of `waittill`/`waittillmatch`/`waittillframeend`/`notify`/`endon` — this is a message-quality improvement only (the keyword already fails to parse as a call-target regardless), not a new safety mechanism; do not frame it as preventing a miscompile that cannot occur.
- [ ] Confirm plain field access/reference AND the pre-existing non-threaded `self set_force_color("c");` form still parse correctly and are unaffected by this new statement-level logic — regression-check against `level.foo`, `level.foo = 1;`, `self.bar`, `self set_force_color("c");`.
- [ ] Extend `DescribeAstNodeKind`/`DumpAst` for the new node kind.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_entity_parser_test"
# Parse REAL lines verbatim (grep -a against the real, device-extracted
# killhouse.gsc and the pulled cargoship_extract.gsc/bog_a_extract.gsc):
#   killhouse.gsc:221   `level thread maps\killhouse_amb::main();`     -- NEW grammar (threaded)
#   cargoship_extract.gsc:172 `level thread maps\cargoship_amb::main();` -- NEW grammar (threaded)
#   bog_a_extract.gsc:807 `self set_force_color( "c" );`                -- regression: already parsed pre-Step-3
#   killhouse.gsc:371   `level waittill ( "mission failed" );`         -> must reject (already fails to parse today; confirm still does)
#   killhouse.gsc:497   `level notify ( "navigationTraining_start" );` -> must reject (already fails to parse today; confirm still does)
#   bog_a_extract.gsc:517 `level endon( "friendlies_take_fire" );`     -> must reject (already fails to parse today; confirm still does)
# Also confirm level.foo / level.foo = 1; / self.bar still parse (regression).
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp \
  "$SCRATCH/script_entity_parser_test/test_entity_parser.cpp" \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  -o "$SCRATCH/script_entity_parser_test/test_entity_parser" && "$SCRATCH/script_entity_parser_test/test_entity_parser"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Both object-prefixed call forms (threaded, non-threaded) parse correctly against real corpus lines.
- `waittill`/`notify`/`endon` (self- or level-prefixed) confirmed to produce a specific, honest error, not a silent miscompile or crash.
- Plain field access/reference confirmed unaffected. All pre-existing suites pass (parser suite especially). NDK build clean.

### Rollback

Additive to the parser (new node kinds, new statement-level disambiguation branch) — revert by removing them; nothing else depends on this yet.

---

## Step 4 — JOIN: compiler wiring (replace all `kEntityDeferred` sites with real codegen)

**Depends on:** Steps 1, 2, 3
**Model tier:** strongest (each surviving call site has different codegen needs, two sites must deliberately NOT change, and the auto-vivification task is genuinely new design — getting all of this right is the step's real risk)

### Context brief

**Corrected by adversarial review — the exact site list and per-site disposition below replaces this plan's earlier "replace all 6 sites with codegen" instruction, which was wrong for two of them.** Grep `IsEntityKeyword`/`kEntityDeferred` in `kisak_script_compiler_android.cpp` fresh to confirm line numbers haven't moved further, but the **per-site disposition itself is settled** (verified against the actual current file during review) — do not re-derive it:

| Site | AST shape | Disposition |
|---|---|---|
| `kEntityDeferred` at (was) :336 | `MethodCallExpr` (non-threaded object-prefixed call, e.g. `self set_force_color("c")`) | **Replace with codegen**: receiver + args + `OP_ScriptMethodCall` |
| `kEntityDeferred` at (was) :340 | `FieldAccessExpr` read | **Replace with codegen**: base expr + `OP_EvalFieldVariable` |
| `kEntityDeferred` at (was) :360 | bare `IdentifierExpr` reference to `self`/`level`/`game` | **Replace with codegen**: `OP_GetSelf`/`OP_GetLevel`/`OP_GetGame` |
| `kEntityDeferred` at (was) :720 | `FieldAccessExpr` assignment target | **Replace with codegen**: base expr + `OP_EvalFieldVariableRef` + value + existing `OP_SetVariableField` |
| `kEntityDeferred` at (was) :761 | assignment to a bare object keyword (`level = x;`) | **Keep rejected** — Scope Cut item 4; no corpus usage, no sensible semantics |
| `IsEntityKeyword` guard at (was) :274 | `CollectLocals`'s `!IsEntityKeyword(name) && !out.Has(name)` | **Keep unchanged** — this prevents `self`/`level`/`game` from being registered as function locals; they are not locals, and "codegen" is meaningless here. Touching this guard would be a regression, not progress. |
| `IsEntityKeyword` guard at (was) :431 | `++`/`--` operand check | **Keep rejected** — incrementing/decrementing a whole object has no real-retail semantics this port needs; not in corpus. |

New object-prefixed-call codegen mapping (for the `MethodCallExpr` site above and Step 3's new threaded node):
- `MethodCallExpr` (already parses today, per Architecture fact 7/H2 correction) → emit receiver expr, emit args (reuse `EmitCall`'s existing argument-emission logic), emit `OP_ScriptMethodCall`.
- Step 3's new threaded node → emit receiver expr, emit args, emit `OP_ScriptMethodThreadCall`, then a trailing `OP_DecTop` to discard the unused return value, mirroring the threading blueprint's own resolved discard mechanism exactly.
- If the object-prefixed call's target resolves to the local-`FunctionRef`-pointer tier — reject with a clear, specific compile error (`OP_ScriptMethodCallPointer`/`OP_ScriptMethodThreadCallPointer` are deliberately unimplemented, per Step 2), matching the non-method call forms' existing precedent. Check the real corpus first to confirm whether this actually occurs before spending effort polishing its error message.

**Auto-vivification (new, load-bearing task — corrected by adversarial review, this is NOT "for free"):** `level.fogvalue["near"] = 100;` (cargoship's own first `main()` statement, no prior `level.fogvalue = [];`) is an assignment through an `ArrayIndexExpr` whose base is a `FieldAccessExpr` on an **unset** field. Today's array-assignment codegen (`EmitAssignment`'s `ArrayIndexExpr` branch) emits the base via the ordinary **read** path, which — per Architecture fact 4 — yields `Undefined` for an unset field; `OP_EvalArrayRef` then `RuntimeError`s on a non-`Array` operand. Real GSC auto-vivifies: an indexed write through an unset field must **create** a fresh `Array`, store it into the field, and yield a ref into that new array in the same operation. This requires either (a) a dedicated codegen path when `EmitAssignment`'s `ArrayIndexExpr` base is itself a `FieldAccessExpr` — emit `OP_EvalFieldVariableRef` for the base (not the plain read opcode), and give the VM-side ref-resolution an auto-vivify behavior when the field is currently `Undefined` (materialize an empty `Array`, store it, then proceed as if it had been there), or (b) an equivalent mechanism you design and document. Do not skip this and do not assume it composes for free — it is the corpus's own headline example and must be verified executing correctly, not just compiling.

### Tasks

- [ ] Grep-confirm the current call-site line numbers, apply the per-site table above exactly — 4 sites become codegen, 3 sites (bare-keyword assignment, the two `IsEntityKeyword` guards) stay rejected/unchanged. Do not touch the `CollectLocals` or `++`/`--` guards.
- [ ] Wire the `MethodCallExpr` site (existing node, newly-supported codegen) AND Step 3's new threaded node into `EmitExpression`/`EmitStatement` per the mapping above.
- [ ] Implement the auto-vivification task described above. Verify explicitly against `level.fogvalue["near"] = 100;` with NO preceding `level.fogvalue = [];` — this is the load-bearing test, not the easier already-initialized case.
- [ ] Separately confirm the already-initialized composition case (`level.arr = []; level.arr[0] = "hi";`) still works — this one plausibly IS free once field-read produces a real `Array`, but confirm rather than assume.
- [ ] Confirm the `waittill`/`notify`/`endon` parse-time rejection from Step 3 is unaffected by this step's codegen changes (it should be untouched, since it fires before these code paths are ever reached).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_entity_step4_test"
# Host test: compile+execute hand-written scripts exercising:
#   level.foo = 5; x = level.foo;                              (field read/write)
#   level.arr = []; level.arr[0] = "hi"; y = level.arr[0];      (field+array composition, pre-initialized)
#   level.fogvalue["near"] = 100; z = level.fogvalue["near"];   (AUTO-VIVIFICATION -- the load-bearing
#                                                                 case, no prior `level.fogvalue = [];`;
#                                                                 corrected by adversarial review, this
#                                                                 is NOT free, see Step 4 Context brief)
#   self.tag = "a"; sub(); (sub reads self.tag)                 (self inheritance through a nested call)
#   target() { self.marked = true; } main() { level thread target(); } (method-thread rebinds self)
#   self set_force_color("c");                                  (non-threaded MethodCallExpr codegen)
# ALSO re-run the REAL corpus lines from Step 3's parser test through the
# full compile+execute pipeline: killhouse.gsc:221, cargoship:172,
# bog_a:807 (isolated with real preceding init lines, matching the arrays
# blueprint's own documented mistake-to-avoid), plus confirm the
# waittill/notify/endon lines still reject cleanly through full compilation.
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_entity_step4_test/test_step4.cpp" \
  android/app/src/main/cpp/kisak_script_compiler_android.cpp \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  android/app/src/main/cpp/kisak_script_entity_android.cpp \
  "$SCRATCH/script_compiler_test/dvar_host_stub.cpp" \
  -o "$SCRATCH/script_entity_step4_test/test_step4" && "$SCRATCH/script_entity_step4_test/test_step4"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- The 4 codegen-eligible sites (per the Step 4 table) replaced with real, tested codegen; the 3 keep-rejected/unchanged sites (bare-keyword assignment, the two `IsEntityKeyword` guards) confirmed untouched.
- Field+array composition confirmed working for BOTH the pre-initialized case AND the auto-vivification case (`level.fogvalue["near"] = 100;` with no prior init) — the latter is the corpus's own real headline example and must execute correctly, not just compile. Self-inheritance through nested calls confirmed (including through `OP_ScriptFunctionCallPointer`, per Step 1's M1 fix). Method-call self-rebinding confirmed for both `MethodCallExpr` (non-threaded) and the new threaded node.
- Real corpus lines from killhouse/cargoship/bog_a compile and execute correctly in isolation; `waittill`/`notify`/`endon` still cleanly rejected at parse time.
- All pre-existing suites pass. NDK build clean on 4 ABIs.

### Rollback

Additive/replacement within `EmitStatement`/`EmitExpression`/`EmitAssignment`'s existing switches — revert by restoring the `kEntityDeferred` rejection at the 4 codegen-eligible sites (per the Step 4 table); nothing outside the compiler depends on this step.

---

## Step 5 — Real-corpus validation

**Depends on:** Step 4
**Model tier:** default

### Context brief

Same role as every prior blueprint's real-corpus validation step. This plan's own research predicts a LARGE jump for killhouse/cargoship specifically (field access was blocking the compile phase from literally its first few lines, once the object-prefixed-thread parse gap is also fixed) — confirm this prediction precisely rather than assuming it, and report the real, measured new failure point for all three files (it will very likely be something entirely new, not another entity-model construct — report exactly what it is). **Note the corrected claim from Step 4**: cargoship executing past its own line 10 (`level.fogvalue["near"] = 100;`) depends specifically on Step 4's auto-vivification task having been implemented, not on field-access/array machinery composing "for free" — if Step 4's auto-vivification task was skipped or is broken, cargoship will advance past its parse gate but RuntimeError at line 10 during execution; that is the concrete, falsifiable thing to check here, not just "does it compile."

### Tasks

- [ ] Re-run the same 3 real levels (killhouse via the real device-extracted `.gsc`, cargoship/bog_a via their pulled `.ff`/extracted files, the established host fastfile/rawfile/compiler pipeline) through the full pipeline. Confirm killhouse and cargoship's shared blocker (`level thread ...`) resolves, and document precisely where each file's failure point lands next.
- [ ] Confirm bog_a's `self set_force_color("c");` line compiles and, more importantly, confirm whatever bog_a's OWN current failure point is (not necessarily entity-model-related) is investigated and documented, even if this blueprint doesn't fix it.
- [ ] Specifically confirm `level.fogvalue["near"] = 100;` (cargoship's own first `main()` statement, an auto-vivification case, not a pre-initialized one) compiles AND **executes** correctly — this is the direct real-corpus test of Step 4's auto-vivification task, not a compile-only check.
- [ ] Update `plans/gscript-real-source-notes.md` with this step's findings, including the real, measured new failure lines for all three files and what they reveal about the next blueprint's own priority.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
bash /tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad/run_all_tests.sh
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- killhouse/cargoship's shared `level thread` blocker confirmed resolved, with concrete new failure lines documented for all three files.
- Field+array-on-field composition confirmed working against real corpus data, not just synthetic snippets.
- `plans/gscript-real-source-notes.md` updated.

### Rollback

Validation-only step; a failure here points back to Steps 1-4.

---

## Step 6 — Device validation + gap-list + memory update

**Depends on:** Step 5
**Model tier:** default

### Context brief

Same role as every prior blueprint's final step. No new subsystem code.

### Tasks

- [ ] Run the full pipeline on real device (`New Game` -> `devmap killhouse`) and confirm: no crash, killhouse's failure point matches Step 5's host-confirmed finding exactly (if it differs, investigate the discrepancy, don't paper over it).
- [ ] Confirm no regressions in the world/gameplay milestones already validated this session (world render, static props/vehicle textures, viewmodel weapon, HUD move-stick; hitscan fire/audio synthetic-input reproduction has been flaky before across multiple sessions — if flaky again, document honestly as inconclusive, matching this project's own established standard).
- [ ] Update `plans/gscript-vm-gaps.md`: mark self/level/game field access and object-prefixed calls as covered (with the explicit `waittill`/`notify`/`endon`/entity-builtin-method/anim carve-outs noted), update the relevant rows in the "Not covered" table, and re-rank the recommendation list based on Step 5's actual finding of what killhouse/cargoship/bog_a now hit next.
- [ ] Update `/home/jacques/.claude/projects/-home-jacques/memory/project_kisakcod_android.md` following the exact structure every prior milestone entry used (French, bold header, what/how/validated, gotchas).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "step9\|thread\|level\|zone de mission"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Device pass confirms no crash, no regressions, and Step 5's exit criteria hold on real device too.
- `plans/gscript-vm-gaps.md` and auto-memory updated with concrete findings.

### Rollback

Documentation-only step; a regression found here is a bug in Steps 1-5.

---

## Plan-level notes for whoever executes this

- **Do not attempt `waittill`/`notify`/`endon`, entity builtin-methods, `anim`, or real cross-`Execute()`-call persistence of `level`/`game`** even if a real script's behavior after this plan makes one of them look tractable — see Scope Cut for why each is deferred.
- **Corrected by adversarial review**: `waittill`/`notify`/`endon` are lexer keywords, not identifiers, so they structurally cannot be misparsed as calls under either the pre-existing non-threaded `MethodCallExpr` grammar or Step 3's new threaded-form grammar — the "silent miscompile" risk this plan originally flagged as its sharpest cannot actually occur, and the remaining work there is error-message polish only. **This plan's actual sharpest real risk is Step 4's field-array auto-vivification task** (`level.fogvalue["near"] = 100;`, cargoship's own first `main()` statement, no prior initialization) — this is genuinely new codegen/VM-ref-resolution design, not something that composes for free from the existing array machinery, and it is the one piece of this plan's central "real corpus mostly falls out for free" thesis that does not hold without deliberate extra work. Do not let a future cold-start agent skip it because an earlier draft of this plan (before review) claimed it was free.
- Every step reuses the established host-then-device workflow: prototype in `/tmp/claude-*/scratchpad` with `g++` before touching the NDK build; re-run `run_all_tests.sh` after every step.
- If the prior blueprints' scratchpad `.gsc`/`.ff`/extracted files didn't survive a session boundary, re-derive them exactly as documented in the arrays/threading plans' own "What already exists" sections — every real-corpus claim in this plan depends on genuine source text, not paraphrase.
