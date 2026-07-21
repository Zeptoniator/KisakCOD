# GScript switch/case/default + loop break/continue — blueprint

Direct mode (no branches/PRs — matches all five prior GScript blueprints this session). Fork remote `fork` = `github.com/Zeptoniator/KisakCOD.git` (writable); `origin` = upstream, read-only.

## Objective

Extend the GScript VM port with `switch`/`case`/`default` statements and loop `break`/`continue` — the confirmed, measured top-priority remaining gap per `plans/gscript-vm-gaps.md` (re-ranked 2026-07-21 after the entity/object-model blueprint shipped and cargoship became, for the first time across six blueprints' worth of real-corpus re-validation, the first real level to ever actually reach `switch` as its measured next compile blocker).

**Real corpus evidence, confirmed by direct extraction (not assumed):**
- `cargoship_extract.gsc:189` is the blueprint's own headline target — `switch(level.jumptosection) { case "bridge": bridge_main(); case "deck": deck_main(); ... }` — **no `break` anywhere in this specific switch**, meaning every case that doesn't match falls through into calling ALL subsequent cases' functions too once the first match is found. This is not a bug in the real script — real GSC's switch has genuine C-style fallthrough semantics (confirmed against retail's own VM dispatch, see below), and this specific construct is almost certainly a debug/QA "jump to any checkpoint" mechanism that deliberately replays every subsequent stage-setup function to bring the game state up to the selected point.
- `switch` occurs in **9 distinct real files** this session's corpus already has extracted (`cargoship_extract.gsc` alone has **15** separate `switch` statements), confirming this is common, not a one-off.
- Real corpus confirms BOTH case-label types: **string** labels (`case "bridge":`, the majority) and **integer** labels (`case 0:` .. `case 4:`, e.g. `ally_sas_woodland_smg_mp5.gsc:26-40`, switching on `codescripts\character::get_random_character(5)`'s return value — note the switch SUBJECT can itself be an arbitrary call expression, not just a field/local read).
- Real corpus confirms **`break` is used 28+ times in `cargoship_extract.gsc` alone**, and confirms the standard pattern (`cargoship_extract.gsc:709-724`: multiple back-to-back switches, each `case`/`default` ending in `break;`).
- Real corpus confirms an optional **brace block after a case label** (`cargoship_extract.gsc:1765`, inside the `switch(jumpto)` statement starting at line 1763: `case "hallways": { ... many statements ... }` — corrected by adversarial review, the earlier draft's "1763" citation pointed at the `switch(...)` line itself, not the braced case) as well as bare (unbraced) statement sequences per case (the majority) and a compact one-line brace-then-break form (`cargoship_extract.gsc:249`: `case 1:{ level.heroes7["alavi"] = ai[i]; }break;`).
- **`continue;` is used 22 times across `bog_a_extract.gsc`/`cargoship_extract.gsc`** (17 in bog_a, 5 in cargoship — corrected by adversarial review; an earlier draft of this plan conflated this count with `break`'s own, unrelated 28-in-cargoship-alone figure), entirely independent of `switch` — this is real, common, everyday loop-control-flow, not a rare construct riding along with switch. Since this port's `for`/`while` currently have NO `break`/`continue` support at all (confirmed: `kisak_script_parser_android.cpp` has zero handling for `KisakScriptKeyword::Break`/`Continue`/`Switch`/`Case`/`Default` today — these fall through to `ParsePrimary`'s generic "unexpected keyword" rejection), this is real, independently-valuable work, not a rider bolted onto switch for convenience.
- **Confirmed by direct grep of the lexer**: `switch`/`case`/`default`/`break`/`continue` are **already real `KisakScriptKeyword` entries** (`kisak_script_lexer_android.h:80`, table populated in `kisak_script_lexer_android.cpp:15-19`, `DescribeKeyword` at lines 294-298) — evidently ported ahead of any grammar support, during an earlier VM-port step. **No lexer work is needed for this blueprint** — confirmed by direct inspection, not assumed; this is the same kind of "already further along than expected" finding the entity-model blueprint had for self/level/game field-access parsing.

## Real retail architecture (confirmed by direct research in `src/script/scr_vm.cpp`, not guessed)

- Retail's `OP_switch`/`OP_endswitch` (`scr_vm.cpp:3337-3391`) implement a genuine **jump table**: the switch subject (must be `VAR_STRING` or `VAR_INTEGER`, else `Scr_Error("cannot switch on %s", ...)`) is compared against a compiled table of `(caseValue, absoluteCodePos)` pairs stored inline in the bytecode, reached via a jump-forward offset embedded in `OP_switch`'s own operand (skipping past the switch's own case-body code to reach the table). The VM scans the table linearly for the first matching `caseValue`; if none match, it falls back to a sentinel entry with `caseValue == 0`, which the compiler always appends and points either at the real `default:` block or at the code immediately following the switch if no `default` exists. Once a target codepos is found, execution jumps there and proceeds **linearly** — real fallthrough, no implicit per-case jump — until either a `break;` (an ordinary unconditional jump past the switch) or the switch's own end is reached. `OP_endswitch` itself is an inert marker whose own operand re-encodes the SAME table (`Scr_ReadIntArray`), skipped over by ordinary linear execution when a case body falls all the way through to the end.
- Retail's `VAR_INTEGER` case-value handling (`scr_vm.cpp:3346-3356`) additionally routes through `IsValidArrayIndex`/`GetInternalVariableIndex` — internals of retail's shared, ref-counted global variable pool (`scrVarGlob`) this port has never replicated (the entity-model blueprint's own `Object`/`Array` value types are deliberately self-contained, NOT built on that pool, exactly per that blueprint's own Architecture facts). This retail-specific indexing has no role here beyond producing a stable 32-bit comparison key for what is, underneath, a plain equality check — this port's own `KisakScriptValue`/`OP_equality` already provide a simpler, sufficient equality mechanism for both String and Int case values.
- `break`/`continue` inside a loop are decompiled-source-invisible (they don't have their own opcodes — they lower to ordinary `OP_jump`/backward jumps at the retail compiler level, the same mechanism this port's own `EmitJumpPlaceholder`/`EmitJumpBackTo` already implement for `if`/`while`/`for`). No VM-level research finding here beyond confirming there is no dedicated break/continue opcode to port.

## Scope cut (read before doing anything else)

**In scope:**
1. `switch (subject) { case V1: ...; case V2: ...; default: ...; }` — real C-style fallthrough (a case with no trailing `break` falls into the next case's statements), both string- and integer-valued case labels (against the SAME switch, matching real retail's dual-type support), an optional `default:` clause (in any position, matching real GSC — confirmed real corpus always places it last but nothing in the grammar should assume that), case bodies with or without a brace block.
2. `break;` inside a `switch` (exits the switch) and inside a `while`/`for` loop (exits the loop) — the nearest enclosing one, matching standard C/real-retail semantics.
3. `continue;` inside a `while`/`for` loop — skips to the next iteration (the condition re-check for `while`; the increment step, THEN the condition re-check, for `for` — matching real C semantics precisely). `continue` always targets the nearest enclosing LOOP specifically, skipping over any intervening `switch` (a `switch` has no continue target of its own).
4. `break;`/`continue;` used outside any loop/switch context is a specific, clear compile error — never a crash, never silently accepted.

**Out of scope, explicit and defensible (matching every prior blueprint's own scope-cut precedent):**
1. **Retail's real jump-table opcode encoding** (`OP_switch`/`OP_endswitch`'s inline table + retail's internal-variable-pool-index case-value trick) — this blueprint instead DESUGARS switch into a chain of ordinary equality-compare-and-conditional-jump sequences, reusing this port's EXISTING `OP_equality`/`OP_JumpOnFalse`/`OP_jump`/`OP_jumpback` opcodes. This preserves the exact same OBSERVABLE behavior (fallthrough, first-match-wins, default fallback) with a much simpler, already-proven-safe emission strategy — matching this port's own established precedent of collapsing retail's more complex internal encodings into simpler equivalents wherever observable behavior is preserved (the arrays blueprint's `OP_EmptyArray`/`OP_EvalArray` collapsing retail's array-opcode family; the entity-model blueprint's `OP_EvalFieldVariable` collapsing retail's several field-access fast-paths). **No new VM opcodes are needed or introduced by this blueprint at all** — a first among this session's GScript blueprints, and worth stating plainly.
2. **`goto`/labeled statements** — GSC has no such construct; retail's grammar doesn't have it either. Not applicable.
3. **`switch` on a vector, float, or other non-String/non-Int subject type** — matches real retail's own restriction (`Scr_Error("cannot switch on %s", ...)` for anything else); this port's compiler will reject the same way, at compile time where the subject's static type is knowable, or the VM will `RuntimeError` identically to how every other type-checked opcode in this port already does.
4. **Multiple case labels sharing one body** (`case "a": case "b": doSomething();`, valid in real C and presumably real GSC) — **no real corpus usage found this session** across 9 files with `switch` — deferred until a real script actually needs it; the grammar's own design should make adding it later a small, additive change (documented in Step 1's own task list as a natural extension point), not a redesign.
5. **`break`/`continue` interacting with the (out-of-scope, per the threading blueprint) real thread-suspension mechanism** — not applicable, since this port's `wait`/`thread` are already documented synchronous-inline simplifications with no scheduler to interact with.

## Architecture facts locked in by research (2026-07-21)

1. **New AST node kinds**: `SwitchStatement` (children[0] = subject expression; remaining children = `CaseClause` nodes, in source order), `CaseClause` (`intValue`/`text`/a dedicated `isDefault` marker to distinguish a `default:` clause from a real case value of coincidentally-matching content; children = the clause's own statement list, NOT wrapped in an extra `Block` node — matches how `FunctionDef`'s own body is a flat statement list under one `Block`, keeping `DumpAst` simple), `BreakStatement` (no children), `ContinueStatement` (no children).
2. **Compiler needs a compile-time "enclosing construct" context stack** — NOT a new runtime/VM concept, purely a compiler-local bookkeeping structure (e.g. `std::vector<LoopOrSwitchContext>` on the `Compiler` struct, pushed at the start of `EmitWhile`/`EmitFor`/`EmitSwitch` and popped at the end) — each entry holds a list of pending `break;` jump-operand positions (patched to "here" once the whole construct's code is emitted). For loop entries specifically, `continue;`'s target is handled DIFFERENTLY per loop kind (corrected by adversarial review, H2 — an earlier draft of this plan assumed a single "stored position" model that only actually works for `while`):
   - **`while`**: the condition re-check position is already KNOWN at the time any `continue;` inside the body is compiled (it's `EmitWhile`'s own `condTop`, captured before the body is emitted) — a `continue;` there emits an immediate `EmitJumpBackTo(condTop)`, no fixup needed, exactly as an earlier draft assumed.
   - **`for`**: the increment clause is emitted AFTER the body (`EmitFor`'s real order is cond → jump-on-false → body → increment → jump-back, confirmed against the actual current function) — so a `continue;` encountered while compiling the body is a **forward reference to a position that does not exist yet**. This CANNOT be "the stored position" the way `while`'s can; it needs its own **continue-fixup list** (the same shape as the break-fixup list: a list of pending forward-jump operand positions), patched via `PatchForward32` at the exact moment `EmitFor` reaches the point between emitting the body and emitting the increment clause. This still runs the increment before re-testing the condition (matching real C semantics), it just requires deferred patching like `break;` does, not an immediate backward jump like `while`'s `continue;` does.
   A `switch` context has NO continue-fixup list and NO continue-target at all (by design — Scope Cut item 3): `continue;` inside a switch must find and target the nearest ENCLOSING LOOP entry in the stack, skipping over any switch entries in between.
3. **Switch desugaring, precisely — SPLIT-DISPATCH LAYOUT** (Scope Cut item 1). **Corrected by adversarial review — this is a CRITICAL fix.** An earlier draft of this plan proposed *interleaving* each case's comparison immediately before that case's own body (`cmp1; body1; cmp2; body2; ...`). Tracing that design against cargoship:189's own real fallthrough cascade proves it is WRONG: it produces the *opposite* of fallthrough — after body1 runs, execution falls into cmp2, which (since the subject only ever equals ONE literal) evaluates false and jumps PAST body2 to cmp3, and so on — every case silently behaves as if it ended in `break`, exactly the failure mode this plan's own Plan-level notes warn is worse than not compiling the construct at all. **The correct layout separates the comparison chain from the bodies entirely**, mirroring retail's own real structure (a jump table reaching contiguous case bodies) using only this port's existing opcodes:
   ```
   ; --- dispatch prologue: one comparison per REAL case, in source order ---
   EvalLocal(hiddenSlot); <push case-1 literal>; OP_equality; OP_JumpOnTrue -> Lbody_1
   EvalLocal(hiddenSlot); <push case-2 literal>; OP_equality; OP_JumpOnTrue -> Lbody_2
   ...
   OP_jump -> Ldefault_or_Lend   ; no case matched
   ; --- bodies, contiguous, in SOURCE order (this is what makes fallthrough automatic) ---
   Lbody_1: <case-1 statements>            (falls into Lbody_2 if no break)
   Lbody_2: <case-2 statements>            (falls into whatever is next in source order)
   ...
   Ldefault: <default statements>          ; wherever `default:` sits in source order, its LABEL
                                            ; position is where it's emitted, but the ONE
                                            ; "no case matched" jump in the dispatch prologue
                                            ; always targets it directly -- no other special-
                                            ; casing needed for a non-last default (this also
                                            ; DISSOLVES the separate non-last-default design
                                            ; problem the earlier draft's Step 3 struggled with)
   ...
   Lend:
   ```
   `OP_JumpOnTrue` pops the equality-comparison bool and jumps to the matched case's body on true; a false result falls through to the NEXT comparison (never into a body) — this is what makes the dispatch prologue safe to fall through across, and it's why bodies must be physically separated from comparisons. The subject is still evaluated ONCE into the hidden slot (Architecture fact 3's own single-evaluation guarantee, still needed for `ally_sas_woodland_smg_mp5.gsc`'s call-expression subject, `codescripts\character::get_random_character(5)` — re-reading the SLOT for every comparison does not re-evaluate the subject expression itself). `break;` inside any body is an unconditional jump to `Lend`, registered via the context stack from fact 2, exactly as before. This entire construct is built from existing opcodes (`OP_equality`/`OP_JumpOnTrue`/`OP_jump`) — no new opcode, no change to Scope Cut item 1's "zero new opcodes" claim.
4. **`break;`/`continue;` codegen**: both compile to a plain unconditional `OP_jump`. `break;` is always a forward placeholder (`EmitJumpPlaceholder`), added to the current context's break-fixup list, patched via `PatchForward32` once that construct's end position is known. `continue;` is either an immediate backward jump (`EmitJumpBackTo`, for `while`) or a forward placeholder added to the enclosing `for` context's OWN continue-fixup list (per fact 2's corrected design), patched via `PatchForward32` once `EmitFor` reaches its own increment-clause position. No new opcode, no new jump-encoding scheme — every primitive already exists. `EmitStatement`'s existing `default:` "cannot compile statement node" fallback does NOT automatically catch a stray `break;`/`continue;` outside any context — this needs an explicit, deliberate check (an empty/loop-only-search-miss context stack), since `BreakStatement`/`ContinueStatement` ARE otherwise valid, compilable node kinds once wired into `EmitStatement` at all.
5. **No VM changes needed at all** (Scope Cut item 1) — this is the first blueprint this session that adds zero new opcodes and touches zero VM dispatch code. All work is parser (new grammar) + compiler (new codegen, reusing existing opcodes end to end).

## De-risking strategy

Same shape as every prior GScript blueprint, adapted to this blueprint's own zero-new-opcode shape: parser grammar first (both new statement families), then compiler wiring for the SIMPLER of the two new families (loop break/continue, which only needs the context-stack machinery applied to the two EXISTING loop constructs), then the JOIN step for switch (which depends on that same context-stack machinery plus its own desugaring design), then real-corpus validation, then device validation.

## Step graph

```
Step 1 (Parser: SwitchStatement/CaseClause grammar + BreakStatement/
         ContinueStatement grammar — both new statement families)
                    |
                    v
Step 2 (Compiler: break/continue context-stack machinery, wired into the
         EXISTING EmitWhile/EmitFor — no switch involved yet)
                    |
                    v
Step 3 (Compiler JOIN: switch statement desugaring — hidden slot + compare-
         and-jump chain + fallthrough + default fallback, wired into the
         SAME context stack from step 2 so break inside a case body works)
                    |
                    v
Step 4 (Real-corpus validation: cargoship's own switch(level.jumptosection)
         fallthrough cascade, the int-cased ally_*.gsc switches, bog_a's
         continue-heavy loops — confirm cargoship's own next failure point
         moves past line 189)
                    |
                    v
Step 5 (Device validation + gap-list + memory update)
```

---

## Step 1 — Parser: switch/case/default + break/continue grammar

**Depends on:** nothing
**Model tier:** strongest (getting the `CaseClause` AST shape right — flat statement list per clause, unambiguous `default:` marker, correct handling of an optional brace block per clause — is this step's real design decision; everything downstream depends on it being right the first time)

### Context brief

Read Architecture fact 1 in full. `switch`/`case`/`default`/`break`/`continue` are ALREADY real lexer keywords (`kisak_script_lexer_android.h:80`, `.cpp:15-19,294-298`) — confirm this yourself with a quick grep before starting, don't just trust this plan's citation, but expect to find it correct and do NOT add any lexer code. Read `ParseIf`/`ParseWhile` (`kisak_script_parser_android.cpp`) as the closest existing structural precedent for a keyword-led compound statement with a parenthesized head expression and one or more nested statement bodies. Real corpus shapes to support (verify by parsing these EXACT lines, not paraphrases): `cargoship_extract.gsc:189-198` (string-cased fallthrough cascade, no braces, no break, no default), `ally_sas_woodland_smg_mp5.gsc:26-41` (int-cased, namespaced-call subject, each case ends in `break;`, no default), `cargoship_extract.gsc:709-716` (a `default:` clause present, ending in `break;`), `cargoship_extract.gsc:1763` (`case "hallways":{ ... }` — a braced case body), `cargoship_extract.gsc:249` (`case 1:{ ... }break;` — braced body immediately followed by `break;` OUTSIDE the brace, on the case's own trailing line — confirm your grammar handles `break;` appearing either inside or immediately after a case's own `{ }` block, since real GSC treats both identically and this exact real line does the latter).

### Tasks

- [ ] Add `SwitchStatement`, `CaseClause`, `BreakStatement`, `ContinueStatement` to `KisakAstNodeKind` (`kisak_script_parser_android.h`), documented per Architecture fact 1's exact shape.
- [ ] `ParseStatement`: dispatch `KisakScriptKeyword::Switch` to a new `ParseSwitch()`, `KisakScriptKeyword::Break`/`Continue` to trivial `MakeNode(...)` + `ExpectOp(";")` (mirroring `ParseReturn`'s own shape for a no-expression statement).
- [ ] `ParseSwitch()`: `switch` `(` subject-expression `)` `{` then repeatedly parse either a `case <expr>:` or `default:` clause header followed by a flat run of ordinary statements (via the EXISTING `ParseStatement()` loop, exactly like `ParseBlock` does — a clause's statements are NOT required to be wrapped in `{ }`, but MAY optionally start with one; if a clause body opens with `{`, that whole `ParseBlock()` result's children should be spliced flat into the `CaseClause`'s own children — do not leave a nested `Block` wrapper, matching Architecture fact 1's "flat statement list" design) until the next `case`/`default`/`}` token is seen.
- [ ] Case values: parse via a restricted expression subset — a plain int/string literal only (matching every real corpus example; if a future real script needs a computed case value, that is new grammar for a later session, not silently accepted here) — reject anything else with a specific, clear parse error.
- [ ] `DescribeAstNodeKind`/`DumpAst`: extend for all 4 new node kinds (fixes `-Wswitch`, matching every prior step's own established pattern).
- [ ] Regression-check that ordinary `if`/`while`/`for`/plain-statement parsing is completely unaffected (no shared code path was touched beyond `ParseStatement`'s own top-level dispatch list, but confirm rather than assume).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_switch_parser_test"
# Parse REAL lines verbatim (grep -a against the real, device-extracted
# killhouse.gsc and the pulled cargoship_extract.gsc/bog_a_extract.gsc, and
# the real aitype/*.gsc files):
#   cargoship_extract.gsc:189-198  (string-cased fallthrough cascade, no break/default)
#   ally_sas_woodland_smg_mp5.gsc:26-41 (int-cased, namespaced-call subject, break, no default)
#   cargoship_extract.gsc:709-716  (default: clause present)
#   cargoship_extract.gsc:1763-1764ish (braced case body)
#   cargoship_extract.gsc:249      (braced body + trailing break outside the brace)
# Also confirm break;/continue; parse as their own node kinds, and that
# if/while/for/plain-statement parsing is unaffected (regression).
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp \
  "$SCRATCH/script_switch_parser_test/test_switch_parser.cpp" \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  -o "$SCRATCH/script_switch_parser_test/test_switch_parser" && "$SCRATCH/script_switch_parser_test/test_switch_parser"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `switch`/`case`/`default`/`break`/`continue` all parse correctly against every real corpus shape cited above.
- `CaseClause`'s children are a flat statement list regardless of whether the source used a brace block or not.
- Plain `if`/`while`/`for`/expression-statement parsing confirmed unaffected. All pre-existing suites pass. NDK build clean on 4 ABIs.

### Rollback

Additive to the parser (new node kinds, new statement-level dispatch branches) — revert by removing them; nothing else depends on this yet.

---

## Step 2 — Compiler: break/continue for while/for loops

**Depends on:** Step 1
**Model tier:** strongest (the for-loop continue-target-is-the-increment-not-the-condition distinction, and getting nested-loop break/continue targeting to the CORRECT nearest enclosing loop rather than an outer one, are real, easy-to-get-subtly-wrong design points)

### Context brief

Read Architecture facts 2 and 4 in full — fact 2 was corrected by adversarial review (H2): a `for` loop's `continue;` is a forward reference to a not-yet-emitted position and needs its own fixup list, it is NOT a single "stored position" the way `while`'s is. Read `EmitWhile`/`EmitFor` (`kisak_script_compiler_android.cpp`) — confirm `EmitFor`'s real emission order yourself (cond → jump-on-false → body → increment → jump-back) before writing anything, since this step's whole design depends on it. Both loops already use `EmitJumpOnFalsePlaceholder`/`EmitJumpPlaceholder`/`EmitJumpBackTo`/`PatchForward16`/`PatchForward32`; this step reuses those exact primitives, it does not invent new ones. Add a `std::vector<LoopOrSwitchContext>` (or your own equivalent name — document it) member to `Compiler`, pushed/popped around `EmitWhile`'s and `EmitFor`'s own bodies. Each context needs: a break-fixup list (both loop kinds), and — ONLY for `for` — a SEPARATE continue-fixup list (same shape as the break-fixup list: pending forward-jump operand positions, patched via `PatchForward32`). `break;` inside EITHER loop type: record an `EmitJumpPlaceholder()`'s operand position into the TOP context's break-fixup list, patched once that loop's own emission finishes (past its own jump-back). `continue;` inside `while`: an IMMEDIATE `EmitJumpBackTo(condTop)` — no fixup needed, the target is already known. `continue;` inside `for`: record an `EmitJumpPlaceholder()`'s operand position into the enclosing `for` context's continue-fixup list; `EmitFor` must patch every entry in that list (via `PatchForward32`) at the EXACT point between finishing the body's emission and starting the increment clause's emission — this makes `continue;` correctly run the increment before the condition re-check, without needing to know the increment's position in advance. `continue;` must search the context stack from the top for the NEAREST entry that is a LOOP (not a switch) — Step 3 will push switch contexts onto the SAME stack with no continue mechanism at all, so `continue;`'s search logic needs to be written correctly from this step onward even though no switch context exists to skip over yet (confirm this with a unit test using a synthetic two-level context stack, not just real loop code, since Step 3 isn't built yet to test the real interaction).

### Tasks

- [ ] Add the context-stack member (break-fixup list on every entry; continue-fixup list ONLY on `for` entries; `while` needs no continue-fixup list at all, just its own known `condTop`) + push/pop around `EmitWhile`/`EmitFor`.
- [ ] Wire `BreakStatement`/`ContinueStatement` into `EmitStatement`: if the context stack is empty, or (for continue) no LOOP entry exists anywhere in it, a specific compile error ("break/continue used outside any loop or switch" / "continue used outside any loop") — never silently accepted, never a crash. Otherwise emit per the design above.
- [ ] `EmitFor` itself must patch its own context's continue-fixup list at the body-then-increment boundary — this is `EmitFor`'s own responsibility, not something `ContinueStatement`'s own emission can do alone (it only records the fixup; `EmitFor` resolves it once the real position is known).
- [ ] `continue;`'s search-for-nearest-loop logic: write it generically (skip any non-loop context entries) even though no switch context can exist yet — Step 3 depends on this being correct without needing to revisit this step's code.
- [ ] Confirm ordinary `if`/`while`/`for` codegen (no break/continue involved) is completely unaffected — the context stack is purely additive bookkeeping, it must not change a single byte of existing loop bytecode when no break/continue is used inside.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_switch_step2_test"
# Host test: compile+execute hand-written scripts exercising:
#   for(i=0;i<10;i++) { if(i==3) break; sum += i; }           -> sum == 0+1+2 == 3
#   for(i=0;i<5;i++) { if(i==2) continue; sum += i; }         -> sum == 0+1+3+4 == 8 (confirms increment still runs after continue)
#   while(i<10) { i++; if(i==5) break; }                       -> i == 5
#   while(i<10) { i++; if(i%2==0) continue; sum += i; }        -> confirms continue re-checks the while condition correctly
#   nested: for(i=0;i<3;i++) { for(j=0;j<3;j++) { if(j==1) break; inner_sum++; } } -> inner break only exits the INNER loop
#   break;/continue; used bare at top level (no loop) -> specific compile error, not a crash
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_switch_step2_test/test_step2.cpp" \
  android/app/src/main/cpp/kisak_script_compiler_android.cpp \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  android/app/src/main/cpp/kisak_script_entity_android.cpp \
  "$SCRATCH/script_compiler_test/dvar_host_stub.cpp" \
  -o "$SCRATCH/script_switch_step2_test/test_step2" && "$SCRATCH/script_switch_step2_test/test_step2"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- `break;`/`continue;` correctly implemented for both `while` and `for`, including the for-loop's continue-still-runs-the-increment semantics and nested-loop correct-target-selection.
- Bare `break;`/`continue;` outside any loop is a specific compile error.
- Ordinary loop codegen with no break/continue is byte-for-byte unaffected. All pre-existing suites pass. NDK build clean.

### Rollback

Additive to `EmitWhile`/`EmitFor`/`EmitStatement` — revert by removing the context-stack member and the two new `EmitStatement` cases; existing loop codegen is untouched otherwise.

---

## Step 3 — Compiler JOIN: switch statement desugaring

**Depends on:** Steps 1, 2
**Model tier:** strongest (this is the plan's central design decision — the hidden-slot mechanism, the fallthrough-preserving jump chain, and correct interoperation with Step 2's break-context stack all need to be right together)

### Context brief

Read Architecture facts 3, 4 in full — fact 3 was CRITICALLY corrected by adversarial review: the split-dispatch layout (comparisons in one contiguous block, bodies in a SEPARATE contiguous block in source order) is the only correct design, not the interleaved comparison-then-body-then-comparison shape an earlier draft wrongly proposed. Re-read the Objective section's cargoship:189 citation — that EXACT construct (string-cased, no break at all, real fallthrough cascading through every subsequent case) is this step's own most important validation target, since a subtly-wrong fallthrough implementation would silently "fix" that construct into non-fallthrough behavior, changing real game logic rather than preserving it; tracing the split-dispatch design by hand against this exact construct BEFORE writing any code is worth the time. Extend `CollectLocals` (or add a sibling pre-scan pass) to reserve one hidden local slot per `SwitchStatement` node, keyed by the node's own address (`const KisakAstNode*` in an `std::unordered_map`) rather than a separately-recomputed counter — two independent traversals (pre-scan and emission) re-deriving "the same" counter is a fragile coupling; keying by pointer identity sidesteps that entirely. Use a slot-name character this port's lexer cannot produce as part of a real identifier (confirm against the lexer's `IsIdentStart`/`IsIdentCont` — a leading `$` is the safe choice; a leading `__` is NOT collision-proof, since `_` is a perfectly ordinary identifier-start character in this grammar and a real script could name a variable `__switch_tmp`). Push a switch context (Architecture fact 2) with NO continue-fixup mechanism at all onto the SAME context stack Step 2 built, so `break;` inside any case body works via the exact same mechanism as a loop's own break, and `continue;` inside a case body correctly searches PAST this switch context to find an enclosing loop (or errors if none exists).

### Tasks

- [ ] Extend the locals pre-scan to reserve a hidden slot per `SwitchStatement`, keyed by AST node pointer (not a recomputed counter — see context brief). Nested switches each get their own distinct slot automatically, since each is a distinct node.
- [ ] `EmitSwitch`, split-dispatch layout (Architecture fact 3's corrected design):
  1. Emit the subject expression once; store into the hidden slot.
  2. Push a switch context (no continue-fixup mechanism) onto the shared context stack.
  3. **Dispatch prologue**: for each non-default `CaseClause` in source order, emit `EvalLocal(hiddenSlot)` + the case's literal value + `OP_equality` + `OP_JumpOnTrue` to a forward-patched label for that clause's body (collect these label-patch positions, one per clause, to resolve once body positions are known). After the last comparison, emit an unconditional `OP_jump` to the `default:` clause's body position if one exists, else to the switch's own end (Lend) — this SINGLE jump is what reaches `default:` correctly regardless of its source position; no other special-casing is needed for a non-last `default:` (real corpus never places one non-last, but the mechanism handles it for free either way, so keep supporting it).
  4. **Body block**: emit every clause's body statements, in source order, immediately after one another with NO jump inserted between them by this step (this omission IS the fallthrough) — immediately BEFORE each clause's body, patch that clause's own `OP_JumpOnTrue` target (from step 3) to "here."
  5. Patch the dispatch prologue's final unconditional jump to the `default:` body's position (or Lend, if no default).
  6. Pop the switch context, patching every collected `break;` jump (Step 2's shared mechanism) to "here" (past the whole body block).
- [ ] Note the dispatch prologue's `OP_JumpOnTrue` operands are `uint16` (`PatchForward16`, max 0xFFFF, matching every other forward-conditional-jump in this compiler) — document this bound plainly; `PatchForward16` already errors cleanly rather than corrupting bytecode if a switch's own body block ever exceeds it, so this is a recognized, understood limit, not a silent risk.
- [ ] Confirm `waittill`/`notify`/`endon`'s existing parse-time rejection and every other prior blueprint's own behavior is completely unaffected (this step only adds a new `EmitStatement` case and a new locals pre-scan extension, touching nothing else).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
SCRATCH=/tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad
mkdir -p "$SCRATCH/script_switch_step3_test"
# Host test: compile+execute hand-written scripts exercising:
#   real cargoship_extract.gsc:189-198's EXACT fallthrough shape (string-cased,
#     no break) -- confirm matching the FIRST case runs EVERY subsequent
#     case's body too, not just the matched one (the load-bearing fallthrough
#     test)
#   real ally_sas_woodland_smg_mp5.gsc:26-41's EXACT shape (int-cased, each
#     case ends in break -- confirms break correctly stops fallthrough)
#   a default: clause, both matched-by-nothing-else and confirmed reached
#   a default: clause placed BEFORE a later real case (non-last position)
#   switch nested inside a for loop: confirm the loop's own continue/break
#     still correctly targets the LOOP, not accidentally the switch
#   a for/while loop nested INSIDE a switch case body: confirm ITS OWN
#     break/continue correctly targets the loop, not the outer switch
#   switch on a type retail itself rejects (e.g. a float) -> compile or
#     runtime error, not silent misbehavior
g++ -std=c++20 -Wall -Wextra -I android/app/src/main/cpp -I "$SCRATCH/script_vm_test" \
  "$SCRATCH/script_switch_step3_test/test_step3.cpp" \
  android/app/src/main/cpp/kisak_script_compiler_android.cpp \
  android/app/src/main/cpp/kisak_script_vm_android.cpp \
  android/app/src/main/cpp/kisak_script_parser_android.cpp \
  android/app/src/main/cpp/kisak_script_lexer_android.cpp \
  android/app/src/main/cpp/kisak_script_entity_android.cpp \
  "$SCRATCH/script_compiler_test/dvar_host_stub.cpp" \
  -o "$SCRATCH/script_switch_step3_test/test_step3" && "$SCRATCH/script_switch_step3_test/test_step3"
bash "$SCRATCH/run_all_tests.sh"
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Real cargoship:189's exact fallthrough behavior confirmed (matching the first case executes every subsequent case's body, exactly as real GSC/retail would).
- Real ally_sas_woodland_smg_mp5.gsc:26-41's exact break-stops-fallthrough behavior confirmed.
- `default:` confirmed reachable both as the last clause and a non-last clause.
- Switch-inside-loop and loop-inside-switch break/continue targeting both confirmed correct (each targets its OWN nearest enclosing construct, not the outer one).
- All pre-existing suites pass. NDK build clean on 4 ABIs.

### Rollback

Additive to `EmitStatement`/the locals pre-scan — revert by removing the `SwitchStatement` case and the pre-scan extension; nothing outside the compiler depends on this step.

---

## Step 4 — Real-corpus validation

**Depends on:** Step 3
**Model tier:** default

### Context brief

Same role as every prior blueprint's real-corpus validation step. This plan's own headline claim is that cargoship's real, measured compile-progress blocker (line 189, confirmed by the entity-model blueprint's own step 5) should now resolve — confirm this precisely against the full real fastfile/rawfile/cross-file pipeline (the `CompileGscZoneEntryPoint` + `ScanZoneRawFiles` harness the entity-model blueprint's own step 5 used, at `step10_multilevel/test_multilevel_crossfile.cpp` in scratch, extend it rather than rebuild it) rather than assuming it, and report cargoship's real, measured NEW failure point (it will very likely be something entirely new, not another switch/loop-control construct — report exactly what it is, per this session's own established rigor for every prior real-corpus validation step).

### Tasks

- [ ] Re-run all 4 real levels (killhouse/cargoship/bog_a/hunted) through the same real pipeline. Confirm cargoship's line-189 blocker resolves, document precisely where cargoship's failure point lands next.
- [ ] Confirm killhouse's own current blocker (line 371, `waittill` — still explicitly out of scope, per the entity-model blueprint's own scope cut) is UNCHANGED, since this blueprint doesn't touch `waittill`/`notify`/`endon` at all.
- [ ] Confirm bog_a's own current blocker (line 106, `/#` debug-block delimiter — still explicitly out of scope, a separate, independent gap) is UNCHANGED, since this blueprint doesn't touch lexer-level `/#` handling at all — but separately confirm, via an ISOLATED snippet (not the full blocked file), that bog_a's own real heavy `continue;` usage (17 real sites) compiles and executes correctly, since that's this blueprint's own real, measured value for that file even though its whole-file compile progress won't visibly move.
- [ ] Update `plans/gscript-real-source-notes.md` with this step's findings, including the real, measured new failure line for cargoship and what it reveals about the next priority.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
bash /tmp/claude-1000/-home-jacques/7696e7d1-52c5-43ee-a523-03db7498501e/scratchpad/run_all_tests.sh
.claude/skills/run-kisakcod-android/driver.sh build
```

### Exit criteria

- Cargoship's line-189 `switch` blocker confirmed resolved, with a concrete new failure line documented.
- killhouse/bog_a/hunted confirmed unchanged, each for the correct, already-understood reason.
- bog_a's own real continue-heavy loop code confirmed compiling and executing correctly in isolation, even though its whole-file progress doesn't move.
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

- [ ] Run the full pipeline on real device (`New Game` -> `devmap killhouse`, and ideally also trigger cargoship if the device data supports it) and confirm: no crash, each level's failure point matches Step 4's host-confirmed finding exactly (if it differs, investigate the discrepancy, don't paper over it).
- [ ] Confirm no regressions in the world/gameplay milestones already validated this session (world render, static props/vehicle textures, viewmodel weapon, HUD move-stick; synthetic-input flakiness for fire/audio/HUD-stick has been well-documented across many prior passes — if flaky again, document honestly as inconclusive, matching this project's own established standard, rather than re-litigating it).
- [ ] Update `plans/gscript-vm-gaps.md`: mark `switch`/`case`/`default` and loop `break`/`continue` as covered, update the "Not covered" table's row, update the opcode-count note (this blueprint adds ZERO new opcodes — the count stays at 83 of 138, worth stating explicitly rather than leaving ambiguous), and re-rank the recommendation list based on Step 4's actual finding of what cargoship now hits next.
- [ ] Update `/home/jacques/.claude/projects/-home-jacques/memory/project_kisakcod_android.md` following the exact structure every prior milestone entry used (French, bold header, what/how/validated, gotchas).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "step9\|switch\|zone de mission"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Device pass confirms no crash, no regressions, and Step 4's exit criteria hold on real device too.
- `plans/gscript-vm-gaps.md` and auto-memory updated with concrete findings.

### Rollback

Documentation-only step; a regression found here is a bug in Steps 1-4.

---

## Plan-level notes for whoever executes this

- **This blueprint adds zero new VM opcodes** — the first among this session's six GScript blueprints to do so. If a future step finds this desugaring approach insufficient for some real construct not yet seen, treat that as a genuine new finding to investigate and document, not evidence the scope cut was wrong going in (it was a deliberate, defensible simplification, not an oversight).
- **Adversarial review caught a CRITICAL bug in this plan's own first-draft design before any code was written**: an earlier draft of Architecture fact 3 proposed interleaving each case's comparison directly before that case's own body — tracing that design against cargoship:189's real fallthrough cascade proves it silently produces the OPPOSITE of fallthrough (every case behaves as if it ended in an implicit `break`). The corrected design (now reflected in Architecture fact 3 and Step 3's own task list) separates the comparison chain from the body block entirely — comparisons all together, bodies all together in source order — which is what actually makes fallthrough automatic. **If you are implementing Step 3, implement the split-dispatch layout as currently written; do not "simplify" it back toward interleaving comparisons and bodies, that is the exact bug that was caught and fixed here.**
- **Adversarially reviewed for-loop `continue;` semantics**: `continue;` inside a `for` loop is a forward reference to the increment clause's position, which does not exist yet at the time the `continue;` is compiled (the body is emitted before the increment). This needs its own fixup list (like `break;`'s), NOT a single stored position the way `while`'s condition-recheck target can be — `while`'s continue target IS known in advance (an immediate backward jump), `for`'s is not (a deferred forward one). Get this distinction right in Step 2.
- **`continue;`'s "find the nearest enclosing LOOP, skipping switch contexts" search logic (Step 2) is this plan's other real, structural risk** — get it right in Step 2, BEFORE Step 3 introduces switch contexts onto the same stack, and write Step 2's own test to exercise the search logic generically (a synthetic mixed context stack), not just with real loop code, so Step 3 doesn't have to revisit Step 2's own design.
- **The cargoship:189 fallthrough construct is this plan's most important single correctness test** — a subtly-wrong implementation that accidentally makes it non-fallthrough would "fix" a real script's actual behavior rather than preserve it, which is a worse outcome than failing to compile it at all. Prioritize getting this construct's EXACT observable behavior right over any other single verification target in this plan.
- Every step reuses the established host-then-device workflow: prototype in `/tmp/claude-*/scratchpad` with `g++` before touching the NDK build; re-run `run_all_tests.sh` after every step.
- If the prior blueprints' scratchpad `.gsc`/`.ff`/extracted files didn't survive a session boundary, re-derive them exactly as documented in the entity-model/arrays/threading plans' own "What already exists" sections — every real-corpus claim in this plan depends on genuine source text, not paraphrase.
