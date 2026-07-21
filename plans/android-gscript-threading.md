# GScript threading (bare `thread`/`wait`) — blueprint

Direct mode (no branches/PRs — matches all three prior GScript blueprints). Fork remote `fork` = `github.com/Zeptoniator/KisakCOD.git` (writable); `origin` = upstream, read-only.

## Objective

Extend the GScript VM port to support GScript's **bare (self-implicit) `thread` call** and **`wait <expr>;`** — the confirmed, measured top-priority remaining gap per `plans/gscript-vm-gaps.md` (re-ranked 2026-07-21 after the arrays blueprint shipped and was re-validated against real data).

Real corpus evidence: `maps/killhouse.gsc`, `maps/bog_a.gsc`, and `maps/cargoship.gsc` **all three** now fail whole-file parsing at the exact same construct family — a bare `thread` keyword — at their new post-arrays failure lines (221/89/158 respectively): `expected ';', got 'thread'`. This is a genuine **parser grammar gap** (no production for `thread` exists at all), not a deferred compile-time rejection the way entity field access is.

**Real per-file failure-line content, confirmed by direct extraction (not assumed):**
- `bog_a.gsc:89`: `thread debug_player_damage();` — **bare**, no object.
- `cargoship.gsc:158`: `thread maps\_pipes::main();` — **bare**, namespaced target.
- `killhouse.gsc:221`: `level thread maps\killhouse_amb::main();` — **object-prefixed** (`level thread ...`).

**Real corpus usage census** (`grep` against the actual, device-extracted `killhouse.gsc` — not a sample, the whole file, re-verified independently during adversarial review): bare `thread funcName(...)` / `thread path\file::func(...)` — **133 occurrences**. Object-prefixed `<expr> thread funcName(...)` (e.g. `level thread ...`, `level.waters thread execDialog(...)`, `node thread anim_first_frame_solo(...)`) — **~36 occurrences** (total non-comment `thread` usages minus the 133 bare ones — an earlier draft of this count, 48, mixed in commented-out lines; corrected here). Plain `wait <number>;` statements — **80 occurrences**. `waittill`/`notify`/`endon` combined — **~52 occurrences** (18 `waittill` + 29 `notify` + 5 `endon`; an earlier draft undercounted this at 25 — corrected here).

## Scope cut (the central decision of this plan — read before doing anything else)

This blueprint covers **bare/self-implicit `thread` calls and `wait <expr>;` only**. It explicitly does NOT cover: **object-prefixed `thread`** (`<expr> thread funcName(...)`, ~36 real sites — including killhouse's own specific failure line), and **`waittill`/`notify`/`endon`** (~52 real sites). Both of those are deferred to the future entity/object-model blueprint. This is a real, measured, defensible cut, not an arbitrary one — justified below, mirroring every prior blueprint's own scope-cut precedent (namespaced-calls' `_blackhawk` cross-zone cut, arrays' plain-variable-only cut).

**Why this split is the right one, not just a convenient one:**

1. **Architecture**: real retail threading (`src/script/scr_vm.cpp`, real dispatch ~2700-3480, NOT the setjmp-guarded decoy switches at ~1823-2100) is a full cooperative multi-thread scheduler — `OP_ScriptThreadCall`/`OP_ScriptMethodThreadCall`/`OP_ScriptThreadCallPointer`/`OP_ScriptMethodThreadCallPointer` (scr_vm.cpp:2855-2929) spawn an INDEPENDENTLY-scheduled execution context via `AllocThread` (`scr_vm.cpp:2860/2879/2902/2929`, `goto thread_call`) — a separate localId/frame-stack, the caller continues immediately, the new thread runs concurrently, not nested. This is architecturally distinct from an ORDINARY (non-threaded) function/method call, which uses `AllocChildThread` instead (`scr_vm.cpp:2790/2806/2823/2843`, `goto function_call`) — a genuinely nested call, not an independent thread; do not conflate the two primitives, `AllocChildThread` is the existing nested-call mechanism this port already implements (`OP_ScriptFunctionCall`), not something this plan's thread-spawn work reuses. `OP_wait`/`OP_waittillFrameEnd` (scr_vm.cpp:2718-2772) SUSPEND the current thread (`goto thread_end` → `goto thread_return`, unwinding to an OUTER per-frame scheduler loop that resumes pending threads later via a time-keyed array, `scrVarPub.timeArrayId`). This port's `KisakScriptVm::Execute()` has no concept of suspend/resume at all — one call runs one function synchronously to completion or to an error, and is invoked exactly once per script trigger from `kisak_android_native_renderer.cpp`, with no per-frame re-entry point. Building genuine concurrency would be a MUCH bigger structural change (a persistent pending-thread list threaded through the render loop, sharing far more state with the renderer than any prior GScript step) than this plan's own scope — **deliberately not attempted here**; see Architecture fact 1 below for the simplification this plan uses instead.
2. **`waittill`/`notify`/`endon` are deeply entangled with the (already deferred, separately-tracked) entity/object model** — EVERY one of them (scr_vm.cpp:3219-3300+) requires a genuine `VAR_POINTER`-typed object argument (`IsFieldObject` checks) to wait-on/notify-against/end-on. This port's `KisakScriptValue` has no entity/object-pointer variant at all (confirmed: `Undefined/Int/Float/String/CodePos/PreCodePos/FunctionRef/Array`), so these three constructs cannot be meaningfully implemented — not even close to correctly — until some real notion of an addressable object exists. Attempting them now would mean either silently faking object identity (dishonest) or blocking on the entity-model blueprint anyway (so there is no real benefit to attempting them here).
3. **Object-prefixed `thread` (`<expr> thread funcName(...)`) is architecturally a "method call" resolution** (an implicit-self binding read off a runtime object value, the SAME mechanism `<obj> methodName(...)` already uses and this compiler already, correctly, rejects with the entity-deferred error) — supporting it properly means the callee needs a real `self` it can reference, which again routes back to the entity model. A shortcut that fakes `self` as an opaque, unreadable value would parse the syntax but not meaningfully support any real script that actually uses `self` inside the threaded function — not worth the complexity for a construct real-corpus evidence shows is roughly a quarter as common as the bare form (~36 vs 133 sites) anyway.
4. **The measured payoff of the bare-only cut is real and large**: bare `thread`/`wait` alone cover **213 of the ~301 total thread/wait/waittill/notify/endon call sites** found in killhouse.gsc (133 + 80 = 213 covered, vs. ~36 + ~52 = ~88 deferred) — and, concretely, **unblock bog_a and cargoship at their EXACT current whole-file failure lines** (both are bare `thread` calls, confirmed by direct extraction above). killhouse's own specific failure line (221) is object-prefixed and will NOT move with this plan alone — that is stated plainly here, not discovered as a surprise later; but killhouse's failure point is expected to advance PAST line 221 once bare thread/wait parse throughout the rest of the file, landing on whatever real construct comes next (confirm this in Step 4, don't assume it).

## Architecture facts locked in by research (2026-07-21)

1. **Bare `thread funcName(args);` (and namespaced `thread path\file::func(args);`) is implemented as a documented, SIMPLIFIED, SYNCHRONOUS-INLINE call — not real concurrency.** Retail's real semantics: the new thread starts running immediately, and the calling code ALSO continues immediately (both run "in parallel" from that point, cooperatively interleaved by the scheduler at `wait`/`waittill` yield points). This port's simplification: a bare `thread` call executes the target function to completion RIGHT THERE (like an ordinary nested call), discards its return value (matching real GSC's own `thread foo();`-as-statement semantics, which never uses the returned value either), and only THEN does the calling code continue. This is a genuine, documented behavioral divergence (no true parallelism) — for scripts whose threaded functions are pure "fire and forget, run some setup" (the overwhelming majority of the 133 real bare-thread call sites in killhouse.gsc: `thread melon_think(); thread turn_off_frag_lights(); thread waters_think(); ...` — sequential setup calls with no cross-thread interaction), this produces IDENTICAL observable behavior to real concurrency, since nothing later in the SAME calling function depends on a race with the threaded function's timing. Document this trade-off precisely in the implementing code's comments (matching the rigor of every prior simplification in this project — FunctionRef, arrays' reference semantics, etc.) — do not silently under-claim or over-claim fidelity.
2. **`wait <expr>;` is implemented as a documented no-op that just continues immediately** — no real delay is modeled (this port's architecture has no per-frame VM re-entry point to suspend into, per the Scope Cut section above). This is a real behavioral divergence (scripts will run "faster" than retail timing) but is NOT a correctness-blocking one for parsing/compiling further into real scripts — most `wait N;` statements in the real corpus (`wait 5;`, `wait .1;`, `wait .5;`) exist to pace visual/gameplay sequencing that this port's own gameplay loop (viewmodel/hitscan/props, not AI/dialogue timing) doesn't depend on yet. Confirm at Step 1 whether retail's `wait` argument type-checking (float or int only, negative-value rejection, `scr_vm.cpp:2718-2745`) is worth replicating even though the VALUE itself is discarded — replicating the type/range validation (reject a string or negative wait argument as a compile/runtime error) costs little and keeps the door open for a REAL scheduler to fill in later without silently having accepted invalid programs in the meantime.
3. **Opcode reuse**: `OP_ScriptThreadCall` (bare no-path) and its interaction with `NamespacedCallExpr`'s existing cross-file/same-file resolution (namespaced-calls blueprint, already shipped) for `thread path\file::func(args);` are the two real opcode/grammar surfaces this plan touches on the call side; `OP_wait` is the single opcode for the wait side. All three are already-cataloged, currently-unimplemented entries in the fixed 139-entry `KisakScriptOpcode` enum (confirmed: `kisak_script_vm_android.h` has `OP_ScriptThreadCall`, `OP_wait` declared, dispatch not yet implemented in `Interpreter::Run()`) — reuse them with the simplified semantics above; do not invent new opcode IDs (matching this port's own established constraint, honored by every opcode implemented across all three prior blueprints).
4. **Grammar — verified precisely, not assumed** (a real correction found during adversarial review, worth stating exactly since it changes Step 2's shape): `thread` IS already a real `KisakScriptKeyword` (`kisak_script_lexer_android.h`'s enum, `KeywordTable()` maps `"thread"` to it) — its statement-level rejection (`expected ';', got 'thread'`) is purely a missing PARSER production, no lexer work needed for it. **`wait` is NOT currently a keyword at all** — it lexes as a plain `Identifier` today (confirmed: no `Wait` entry exists in `KisakScriptKeyword`'s enum or `KeywordTable()`, unlike `Thread`/`Waittill`/`Waittillmatch`/`Waittillframeend`/`Notify`/`Endon`, which are all already there). This means Step 2 must ALSO add a `Wait` keyword to the lexer (enum entry + `KeywordTable()` entry + its `DescribeKeyword`-equivalent string mapping) before any parser grammar for `wait <expr>;` can key off a keyword token — a small, safe lexer change (`wait` is a reserved word in real GSC; promoting an identifier that real scripts never legitimately use as a plain variable name to a keyword is regression-free), but a REAL one, not "purely additive to the parser" as an earlier draft of this plan assumed. `thread <call-expr>;` needs a new statement-level (or expression-level, mirroring how `MethodCallExpr`/postfix `++`/`--` are recognized one level up from `ParsePostfix`) grammar production. `wait <expr>;` is a simple new statement kind, `<expr>` being any expression (real corpus only ever uses int/float literals, but the grammar should accept a general expression per real GSC — confirm this against the corpus, don't over- or under-scope the grammar based on assumption).
5. **Object-prefixed thread (`<expr> thread funcName(...)`) and `waittill`/`notify`/`endon` are explicitly OUT OF SCOPE for this plan** (see Scope Cut section) — if the parser happens to encounter `<expr> thread ...` before this plan's own grammar work, it should produce a clear, specific parse/compile error (not silently misparse as something else), the same honesty standard as every other deferred construct in this project. Do not attempt to make these "sort of work" via a shortcut.

## De-risking strategy

Same shape as every prior GScript blueprint: VM opcode semantics first (hand-assembled bytecode, no parser/compiler needed), parser grammar in parallel, then a compiler join point, then real-corpus validation, then device.

## Step graph

```
Step 1 (VM: OP_ScriptThreadCall synchronous-inline semantics + OP_wait no-op, with type/range validation)
Step 2 (Parser: `thread <call-expr>;` statement, `wait <expr>;` statement)          } run in parallel
                    |
                    v
Step 3 (JOIN: compiler wiring — EmitStatement for both, reusing NamespacedCallExpr/CallExpr's existing arg-emission logic for the threaded call target)
                    |
                    v
Step 4 (Real-corpus validation: killhouse/bog_a/cargoship re-run, confirm bog_a/cargoship's EXACT failure lines resolve, confirm killhouse's failure point advances past line 221 to something new)
                    |
                    v
Step 5 (Device validation + gap-list + memory update)
```

---

## Step 1 — VM: `OP_ScriptThreadCall` (synchronous-inline) + `OP_wait` (no-op, validated)

**Depends on:** nothing (parallel-eligible with Step 2)
**Model tier:** default

### Context brief

Read Architecture facts 1-3 in full before starting. Read `kisak_script_vm_android.cpp`'s existing `OP_ScriptFunctionCall` dispatch (the frame-push machinery you're reusing verbatim for the "synchronous-inline" thread-call simplification) and `OP_PreScriptCall`/argument-binding conventions before writing `OP_ScriptThreadCall`'s dispatch — it should be functionally identical to `OP_ScriptFunctionCall`. **The discard-mechanism design question this Architecture fact used to leave open is already resolved** (see the Step 1 task list below) — there is no return-value hook available at `OP_ScriptThreadCall`'s own dispatch site (the callee hasn't run yet at that point; its return value only surfaces later, at its own `OP_Return`/`OP_End`), so the discard is necessarily the COMPILER's job (Step 3), via a trailing `OP_DecTop` exactly like `EmitExprClause` already does for any other statement-level call whose result goes unused.

### Tasks

- [ ] Implement `OP_ScriptThreadCall` in `Interpreter::Run()`: **byte-identical dispatch to `OP_ScriptFunctionCall`** (same frame-push/argument-binding mechanics, reuse the exact sequence with a comment explaining the reuse and why — the two opcodes differ semantically only in retail, where thread-calls spawn an independent execution context via `AllocThread` rather than nesting via `AllocChildThread`; this port's synchronous-inline simplification means there is no such distinction to implement at the VM level at all). **The discard mechanism is resolved, not left open**: `OP_ScriptThreadCall` itself has no hook to discard a return value at its own dispatch site — at the moment this opcode runs, the callee hasn't executed yet (its return value only surfaces LATER, when it reaches `OP_Return`/`OP_End`, which write into the caller's stack slot that was reserved at the call site) — so there is nothing for the opcode itself to pop "after the callee's frame unwinds," since dispatch doesn't re-enter after a callee returns. The correct, ONLY viable mechanism is Step 3's compiler emitting a trailing `OP_DecTop` after the call, exactly the way `EmitExprClause` (`kisak_script_compiler_android.cpp`) already discards any other unused expression result — this step's job is purely the identical-to-`OP_ScriptFunctionCall` dispatch; Step 3 owns the discard.
- [ ] Implement `OP_wait`: pop one value (the wait duration); type-check it (`Int` or `Float`, matching retail's `scr_vm.cpp:2718-2737`; anything else → `RuntimeError`, matching retail's `Scr_Error` on a bad wait argument); reject a negative value (`RuntimeError`, matching retail's explicit "negative wait is not allowed" check) — then do nothing further (no real delay, no suspend, no scheduler interaction). Document precisely, in the opcode's own comment, that this validates but does not implement real timing, and why (Architecture fact 2 / the Scope Cut section).
- [ ] Do NOT implement `OP_waittillFrameEnd`, `OP_waittill`, `OP_waittillmatch`, `OP_notify`, `OP_endon`, `OP_ScriptMethodThreadCall`, `OP_ScriptThreadCallPointer`, or `OP_ScriptMethodThreadCallPointer` in this step — all explicitly out of scope per this plan's own Scope Cut section (the method/object-prefixed variants need a `self`-binding mechanism this port doesn't have; the wait/notify family needs the entity model). Leave them logged as unsupported, matching this port's established "specific error, not silent misdispatch" pattern.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_thread_test"
# Hand-assembled bytecode test (script_vm_test/kisak_script_asm_test.h pattern):
#   - OP_ScriptThreadCall to a function that mutates a value visible after
#     it returns (proves synchronous-inline execution: the mutation IS
#     visible immediately after the thread-call instruction, unlike real
#     concurrency where timing would be unspecified)
#   - the threaded function's return value is correctly discarded (does not
#     leak onto the caller's stack, confirm stack depth before/after matches)
#   - OP_wait with a valid Int argument: completes without error, no time
#     elapses (documented no-op)
#   - OP_wait with a valid Float argument: same
#   - OP_wait with a String argument: RuntimeError
#   - OP_wait with a negative argument: RuntimeError
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_thread_test/test_thread_vm.cpp" \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  -o "$SCRATCH/script_thread_test/test_thread_vm" && "$SCRATCH/script_thread_test/test_thread_vm"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `OP_ScriptThreadCall` executes the target function synchronously and discards its return value; `OP_wait` validates its argument type/range and is a documented no-op otherwise.
- Host-tested including the discarded-return-value/stack-depth check.
- All pre-existing host suites pass. NDK build clean on 4 ABIs.

### Rollback

Purely additive to `kisak_script_vm_android.cpp` (new dispatch cases) — revert by removing them; nothing else depends on this step yet.

---

## Step 2 — Lexer + Parser: `thread <call-expr>;` and `wait <expr>;` statements

**Depends on:** nothing (parallel-eligible with Step 1)
**Model tier:** default

### Context brief

**Verified precisely already, do not re-derive from scratch** (this changes the step's shape from an original draft, so note it explicitly): `thread` IS already a real `KisakScriptKeyword` (`kisak_script_lexer_android.h`'s enum + `KeywordTable()` in the `.cpp`) — only a parser production is missing for it. **`wait` is NOT currently a keyword** — it lexes as a plain `Identifier` today (no `Wait` entry exists anywhere in the lexer). This step therefore touches the LEXER too, not just the parser: add `Wait` to `KisakScriptKeyword`'s enum, to `KeywordTable()` (map `"wait"` → `KisakScriptKeyword::Wait`), and to whatever `DescribeKeyword`-equivalent function maps the enum back to its string (mirror the exact pattern already used for `Thread`/`Waittill`/etc., which sit right next to where `Wait` belongs). This is a safe, additive lexer change — `wait` is a reserved word in real GSC, so promoting the identifier to a keyword cannot break any real script that used `wait` as an ordinary variable name (it never legitimately could, in real GSC). Read `kisak_script_parser_android.cpp`'s statement-parsing entry point (wherever `IfStatement`/`WhileStatement`/`ReturnStatement` etc. are dispatched from) to see where a new `ThreadCallStatement`/`WaitStatement` production belongs.

### Tasks

- [ ] Add `Wait` to `KisakScriptKeyword`'s enum (`kisak_script_lexer_android.h`) and to `KeywordTable()` + its keyword-to-string mapping (`kisak_script_lexer_android.cpp`) — mirror the exact existing pattern for `Thread`/`Waittill`/`Waittillmatch`/`Waittillframeend`/`Notify`/`Endon` precisely (same file, same functions, adjacent entries).
- [ ] Add `ThreadCallStatement` to `KisakAstNodeKind` (children[0] = the call expression being threaded — reuse `CallExpr`/`NamespacedCallExpr`, whichever the target call actually parses as, unchanged; no new call-parsing logic needed, `thread` is purely a statement-level prefix wrapping an EXISTING call-expression production). Parse: at statement level, if the `thread` keyword is seen, consume it, parse a call expression (bare `CallExpr` or namespaced `NamespacedCallExpr` — reuse `ParsePrimary`'s existing call-parsing paths directly, do not reimplement), wrap in `ThreadCallStatement`, expect `;`.
- [ ] Add `WaitStatement` to `KisakAstNodeKind` (children[0] = the duration expression). Parse: at statement level, if the (now-real) `wait` keyword is seen, consume it, parse a full expression, expect `;`.
- [ ] Confirm (do not assume) that an object-prefixed `<expr> thread funcName(...)` is NOT accidentally accepted by whatever grammar hook you add — if your statement-level `thread`-keyword check fires only when `thread` is the VERY FIRST token of a statement, this is naturally already excluded (an object-prefixed thread has an expression BEFORE the `thread` keyword, so it would be parsed as an ordinary expression statement attempt first, and correctly fail — confirm this is what actually happens, and that the resulting error is clear, not confusing). Per the Scope Cut section, `<expr> thread ...` should still produce a clear, honest parse or compile error (not silently succeed, not crash) — verify precisely what error results and confirm it's reasonable, adjusting the error message if it's misleading (e.g. if it currently just says "expected ';'" without any hint that `thread`-after-an-expression is a real, deferred construct, consider whether a more specific message is warranted — this is a judgment call, document whichever choice is made).
- [ ] Extend `DescribeAstNodeKind`/`DumpAst` for the 2 new node kinds (fixes the `-Wswitch` warning, matching every prior step's pattern — confirm whether `DumpAst`'s existing generic children-recursion already handles these with no special-casing needed, the way it did for the arrays blueprint's new node kinds, before adding anything).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_thread_parser_test"
# Parse REAL lines verbatim (sed -n against the real, device-extracted
# killhouse.gsc and the pulled bog_a.ff/cargoship.ff via the established
# fastfile/rawfile host pipeline in scratchpad/step10_multilevel/):
#   bog_a.gsc:89   `thread debug_player_damage();` (bare, no path)
#   cargoship.gsc:158 `thread maps\_pipes::main();` (bare, namespaced)
#   killhouse.gsc's own bare thread lines (e.g. line 313
#   `thread training_targetDummies( "rifle" );`, an argument-taking bare
#   call) and multiple `wait N;`/`wait .1;` lines (e.g. line 392 `wait 5;`,
#   line 410 `wait .1;`)
# Dump the AST for each, confirm ThreadCallStatement/WaitStatement appear
# with the right shape. ALSO confirm killhouse.gsc's own line 221
# (`level thread maps\killhouse_amb::main();`, object-prefixed) produces a
# clear, specific error, not a crash or a confusing message.
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp \
  "$SCRATCH/script_thread_parser_test/test_thread_parser.cpp" \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  -o "$SCRATCH/script_thread_parser_test/test_thread_parser" && "$SCRATCH/script_thread_parser_test/test_thread_parser"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `thread <call>;` (bare and namespaced) and `wait <expr>;` both parse correctly, verified against real corpus lines from all three real levels, not just synthetic ones.
- Object-prefixed `<expr> thread ...` confirmed to produce a clear, non-crashing, honest error.
- All pre-existing suites pass (parser suite especially). NDK build clean.

### Rollback

Additive to both the lexer (one new keyword) and the parser (new node kinds, new statement-level parse branches) — revert by removing the added keyword entry and parse branches; nothing else depends on either yet.

---

## Step 3 — JOIN: compiler wiring for `ThreadCallStatement`/`WaitStatement`

**Depends on:** Steps 1, 2
**Model tier:** default

### Context brief

Smaller join point than the namespaced-calls or arrays blueprints' — the hard design decisions (synchronous-inline semantics, discard-return-value mechanics resolved to a trailing `OP_DecTop`, no-op wait validation) were already made and documented in Step 1's own work; this step is mostly straightforward emission wiring. Read `kisak_script_compiler_android.cpp`'s `EmitStatement`'s switch, `EmitExprClause` (the existing "call as a statement, discard the result" pattern you're mirroring), and `EmitCall`/`EmitNamespacedCall` (the existing call-argument-emission logic you're reusing for the threaded call's target — note `EmitCall`'s THIRD tier, the local-`FunctionRef`-pointer call via `OP_ScriptFunctionCallPointer`, has no thread-call equivalent in this plan's scope — `OP_ScriptThreadCallPointer` is explicitly NOT implemented per Step 1 — so a `thread <localVar>();` form must be rejected with a clear, specific compile error, not silently miscompiled into a same-file/pointer call; check whether the real corpus ever threads a local variable before deciding how much effort this edge case deserves) before starting.

### Tasks

- [ ] Add a case for `ThreadCallStatement` to `EmitStatement`'s switch: emit the SAME argument-evaluation sequence an ordinary call to the same target would use (reuse `EmitCall`/`EmitNamespacedCall`'s existing logic directly rather than duplicating it — check whether they can be called as-is with the call expression node, or whether a small parameterization is needed to swap `OP_ScriptFunctionCall` for `OP_ScriptThreadCall` at the one emission site that currently hardcodes it — this is the step's one real design decision, document whichever approach is chosen), then a trailing `OP_DecTop` to discard the (unused, per real GSC's own `thread foo();`-as-statement semantics) return value — mirroring `EmitExprClause`'s existing discard pattern exactly, this is Step 1's already-resolved design, not a new decision.
- [ ] If the threaded call target resolves to `EmitCall`'s THIRD tier (a local variable holding a `FunctionRef`, which would normally emit `OP_ScriptFunctionCallPointer`) — reject with a clear, specific compile error (`OP_ScriptThreadCallPointer` is deliberately not implemented, per Step 1) rather than silently emitting a wrong opcode. Check the real corpus first (the established `grep -a` pattern against the real, device-extracted `.gsc` files) to confirm whether `thread <localVar>();` actually occurs anywhere real — if it doesn't, this is a clean, low-stakes scope cut to state plainly, not a gap to agonize over.
- [ ] Add a case for `WaitStatement`: emit the duration expression, then `OP_wait`.
- [ ] Confirm real killhouse.gsc's bare thread-with-namespaced-target form (`thread maps\_introscreen::introscreen_feed_lines( lines );`) compiles correctly through this wiring — this exercises `ThreadCallStatement` wrapping a `NamespacedCallExpr` specifically, not just a bare `CallExpr`, and should reuse the namespaced-calls blueprint's existing same-file/cross-file resolution unchanged.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_thread_step3_test"
# Host test: compile+execute a hand-written script exercising
#   addone(n) { return n + 1; }
#   main() { x = 0; thread addone(x); wait 1; setdvar("done", "yes"); }
# confirm it compiles, runs to completion, and setdvar fires (proving
# thread+wait don't block/crash the calling function's continuation).
# ALSO re-run the REAL corpus lines from Step 2's parser test through the
# full compile+execute pipeline (bog_a.gsc:89, cargoship.gsc:158, and
# killhouse.gsc's own bare thread/wait lines, isolated the same way every
# prior blueprint isolated real-corpus snippets from unrelated gaps).
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_thread_step3_test/test_step3.cpp" \
  android/app/src/main/cpp/kisak_script_compiler_android.cpp \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  android/app/src/main/cpp/kisak_script_entity_android.cpp \
  "$SCRATCH/script_compiler_test/dvar_host_stub.cpp" \
  -o "$SCRATCH/script_thread_step3_test/test_step3" && "$SCRATCH/script_thread_step3_test/test_step3"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Bare and namespaced `thread`/`wait` statements compile and execute correctly end to end, host-verified, including the real bog_a/cargoship failure-line constructs in isolation.
- All pre-existing suites pass. NDK build clean on 4 ABIs.

### Rollback

Purely additive to the compiler's `EmitStatement` switch — revert by removing the two new cases.

---

## Step 4 — Real-corpus validation

**Depends on:** Step 3
**Model tier:** default

### Context brief

Same role as every prior blueprint's real-corpus validation step. Confirm the plan's own predictions precisely — this plan is explicit that killhouse's own specific failure line (221, object-prefixed) will NOT move, while bog_a/cargoship SHOULD advance past their exact current lines (both bare-thread). Confirm this, and confirm exactly where killhouse's failure point lands next (don't assume it's another `thread`-related line — it could be anything).

### Tasks

- [ ] Re-run the SAME 3 real levels (killhouse via the real device-extracted `.gsc`, bog_a/cargoship via their pulled `.ff` files, the established host fastfile/rawfile/compiler pipeline in `scratchpad/step10_multilevel/`) through the full pipeline. Confirm bog_a and cargoship's failure points move PAST their current lines (89/158) — document the new lines and constructs precisely. Confirm killhouse's failure point moves PAST line 221 — document exactly where it lands next (this could be another object-prefixed `thread` line, a `waittill`/`notify`/`endon` line, or something entirely unrelated — report the real, measured result, not a guess).
- [ ] Extract and compile the specific real bare-thread/wait lines cited in this plan's Objective/Scope-Cut sections as isolated snippets, confirming each compiles and executes correctly.
- [ ] Update `plans/gscript-real-source-notes.md` with this step's findings (matching the addendum pattern every prior step used) — including, if killhouse's new failure point reveals anything about the real relative frequency/urgency of object-prefixed thread vs. waittill/notify/endon vs. some other gap, note it for the next blueprint's own prioritization.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
bash /tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad/run_all_tests.sh
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- bog_a/cargoship's failure points confirmed to move past lines 89/158, with a concrete new line+construct documented for each.
- killhouse's failure point confirmed to move past line 221, with a concrete new line+construct documented.
- `plans/gscript-real-source-notes.md` updated.

### Rollback

Validation-only step; a failure here points back to Steps 1-3.

---

## Step 5 — Device validation + gap-list + memory update

**Depends on:** Step 4
**Model tier:** default

### Context brief

Same role as every prior blueprint's final step. No new subsystem code.

### Tasks

- [ ] Run the full pipeline on real device (`New Game` -> `devmap killhouse`) and confirm: no crash, killhouse's failure point matches Step 4's host-confirmed finding exactly (if it differs, investigate the discrepancy, don't paper over it).
- [ ] Confirm no regressions in the world/gameplay milestones already validated this session (world render, static props/vehicle textures, viewmodel weapon, HUD move-stick; hitscan fire/audio synthetic-input reproduction has been flaky before across multiple sessions now — if flaky again, document honestly as inconclusive rather than claiming a false pass).
- [ ] Update `plans/gscript-vm-gaps.md`: mark bare `thread`/`wait` as covered (with the explicit object-thread/waittill/notify/endon carve-out noted), update the threading row in the "Not covered" table, and re-rank the recommendation list based on Step 4's actual finding of what killhouse now hits next.
- [ ] Update `/home/jacques/.claude/projects/-home-jacques/memory/project_kisakcod_android.md` following the exact structure every prior milestone entry used (French, bold header, what/how/validated, gotchas).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "step9\|thread\|zone de mission"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Device pass confirms no crash, no regressions, and the exit criteria from Step 4 hold on real device too.
- `plans/gscript-vm-gaps.md` and auto-memory updated with concrete findings.

### Rollback

Documentation-only step; a regression found here is a bug in Steps 1-4.

---

## Plan-level notes for whoever executes this

- **Do not attempt object-prefixed `thread`, `waittill`, `notify`, or `endon`** even if a real script's behavior after this plan makes one of them look tractable — they need the entity/object-model blueprint's own `self` mechanism, not a shortcut here. Real corpus evidence (~36 + ~52 = ~88 sites) confirms they're real and eventually necessary, just not this plan's job.
- **Do not attempt a real cooperative scheduler** (genuine suspend/resume across frames) — this port's architecture (one synchronous `Execute()` call per script trigger, no per-frame VM re-entry point) would need a much bigger structural change than this plan's scope; the synchronous-inline simplification is the documented, deliberate choice.
- Every step reuses the established host-then-device workflow: prototype in `/tmp/claude-*/scratchpad` with `g++` before touching the NDK build; re-run `run_all_tests.sh` after every step.
- If the prior blueprints' scratchpad `.gsc`/`.ff` files didn't survive a session boundary, re-derive them exactly as documented in the arrays/namespaced-calls plans' own "What already exists" sections — every real-corpus claim in this plan depends on genuine source text, not paraphrase.
