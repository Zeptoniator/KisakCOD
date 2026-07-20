# Blueprint: Namespaced calls + function pointers for the GScript VM port

**Status:** draft
**Created:** 2026-07-21
**Repo:** `/home/jacques/Projects/KisakCOD/` — fork remote `fork` → `github.com/Zeptoniator/KisakCOD.git` (writable), `origin` → `github.com/SwagSoftware/KisakCOD.git` (upstream, **read-only** — do not target `origin` with any PR from this plan)
**Base branch for all steps:** `android-port-bootstrap` (the long-lived integration branch for the whole Android port and the prior GScript VM blueprint; every step branches from and merges back into it)
**Mode:** git + gh CLI available → full branch/PR/CI workflow, direct-to-`fork` (see remote note above)

## Objective

Extend the completed GScript VM port (`plans/android-gscript-vm-port.md`, all
10 steps done, see `plans/gscript-vm-gaps.md` for the full coverage
reference) to support **namespaced function calls** (`path\file::func(args)`,
e.g. `maps\_blackhawk::main()`) and **bare function pointers** (`::func`,
e.g. `default_start(::inside_start)`).

This is `gscript-vm-gaps.md`'s #1-priority finding: of the 4 real mission
scripts validated end-to-end in that plan's Step 10, **2 of 4** (killhouse,
line 27; bog_a, line 43) fail on this construct FIRST — the other two hit a
different gap first (cargoship fails on an array subscript, line 10;
hunted fails on an undirected `#`-directive, line 5; both before any `::`
reference would even be reached). Two-of-four hitting it first, plus the
fact that `default_start`/`add_start(..., ::funcname, ...)` is the standard
mission-init idiom seen at the very top of nearly every SP mission's
`main()` (strong inference, not measured), still makes this the
single highest-leverage NEXT step once a script gets past whatever it
currently hits first — but it is not, on the measured data, "earlier than
any other gap" universally. State it accurately when reporting progress:
this plan fixes a real, common, early blocker, not necessarily *the*
first blocker for every mission.

**Explicit scope cut:** this plan does NOT add entity/object field access
(`self.field`, `level.field`), arrays, `switch`, or threading (`waittill`/
`notify`/`endon`) — those remain separate future blueprints per
`gscript-vm-gaps.md`'s own recommended order (this plan is #1, arrays #2,
entity model #3). A real mission script will very likely still fail on ONE
of those other gaps once this plan lands — that is expected and out of
scope; this plan's success criterion is specifically "gets past `::`."

**Second, more fundamental scope cut, found during this plan's own adversarial
review — read before starting Step 4/5/6**: `maps\_blackhawk::main()`
(killhouse.gsc line 209, this plan's own original headline example) references
`maps\_blackhawk.gsc`, which is a shared cross-mission vehicle script — and it
is **not present in killhouse.ff's rawfiles at all** (confirmed: killhouse.ff's
30 rawfiles include no `_blackhawk`/`_load`/`_80s_hatch1`/`_bus`/
`_bm21_troops`/`_humvee`/`_small_wagon` — the vehicle-init calls at lines
209-219 — nor is it in `code_post_gfx.ff`, the shared zone, which
`gscript-real-source-notes.md` already confirmed has no `maps\*.gsc` at all).
Retail loads these from the filesystem at large; this port only ever sources
`.gsc` from a zone's own rawfile scan, and has no broader "load a .gsc from
anywhere" primitive. **This plan cannot make `_blackhawk::main()` resolve** —
that would need a genuinely separate capability (loading `.gsc` from outside
the currently-scanned zone, if such data exists on-device at all) which is
out of scope here. This plan's real, achievable target is namespaced calls
whose target IS in the same zone (confirmed present in killhouse.ff:
`killhouse_fx`, `killhouse_anim`, `createart\killhouse_art` — all referenced
by killhouse.gsc's own `main()` at lines 218/220/224). Missing-file
namespaced calls (like `_blackhawk`) are expected to become a clean, specific
"file not found" COMPILE error post-this-plan — a real category change (from
a syntax/parse error to a semantic/missing-data error) and genuine, honest
progress, but not full resolution. See Step 6 for how this reshapes the
device-validation exit criteria.

## Why this is a blueprint and not a single PR

Research into the reference engine (`src/script/scr_compiler2.cpp`,
`scr_vm.cpp`, `scr_main.cpp` — see "Architecture facts" below) found that
retail's namespaced-call resolution is NOT a small parser tweak: it requires
a shared multi-file compile session (chain-loading a referenced file the
moment it's seen, compiling all files into one shared bytecode buffer with a
cross-file symbol table, and backpatching forward references). The current
port compiles exactly one in-memory `.gsc` string into one isolated
`KisakScriptProgram` per level (`kisak_script_compiler_android.h`'s
`CompileGscSource`, wired from `StartWorldLoad` in
`kisak_android_native_renderer.cpp`) — there is no multi-file model at all
today. Building one is a genuinely new subsystem, sized similarly to the
original VM/compiler work, not a follow-up patch.

## What already exists (read before starting ANY step)

As of commit `7fe2d31` on `android-port-bootstrap` (the last commit of the
prior GScript VM blueprint):

- **Lexer** (`kisak_script_lexer_android.{h,cpp}`) already tokenizes `::` as
  a 2-char `Operator` token and `\` as a 1-char `Operator` token (path
  separator) — both already confirmed correct against the real `.gsc`
  corpus in the prior plan's Step 6. **No lexer changes needed for this
  plan.**
- **Parser** (`kisak_script_parser_android.{h,cpp}`) has NO grammar
  production for bare `::func` or `path\file::func(args)` today — both
  explicitly deferred. `ParsePrimary`'s `Operator` case has a `Fail(...)`
  fallthrough for any operator it doesn't recognize as the start of an
  expression (including `::`), and `\` is likewise unhandled outside
  `SkipIncludeDirective`. See `KisakAstNodeKind` for the existing node set
  (`Program/FunctionDef/Block/IfStatement/WhileStatement/ForStatement/
  ReturnStatement/ExpressionStatement/Assignment/BinaryExpr/UnaryExpr/
  CallExpr/MethodCallExpr/FieldAccessExpr/IdentifierExpr/<literals>`) — this
  plan adds new kinds to this enum, it does not touch any existing kind's
  meaning.
- **Compiler** (`kisak_script_compiler_android.{h,cpp}`) resolves a
  `CallExpr`'s name via `KisakScriptFindBuiltinIndex` first (→
  `OP_CallBuiltin*`), then a same-`Program` function name (→
  `OP_ScriptFunctionCall` with a backpatched 4-byte codepos operand — see
  `CallFixup`/`callFixups` in `kisak_script_compiler_android.cpp`), else a
  compile error. **There is no cross-file/cross-zone resolution of any
  kind.** The existing backpatch mechanism (`CallFixup`, resolved once every
  function in the current `Program` has been emitted) is the direct
  precedent this plan's cross-file backpatch list should follow — same
  idea, one level up.
- **VM** (`kisak_script_vm_android.{h,cpp}`) — `KisakScriptValue`/
  `KisakScriptValueType` has variants `Undefined/Int/Float/String/CodePos/
  PreCodePos`. **No function-pointer/function-reference variant exists.**
  `KisakScriptOpcode` already has the FULL retail opcode set defined
  (`OP_GetFunction=0x15`, `OP_ScriptFunctionCallPointer=0x51`,
  `OP_ScriptThreadCallPointer=0x55`, etc. — all 139 entries were ported in
  the original plan's Step 2) but `Interpreter::Run()`'s dispatch switch
  does not implement `OP_GetFunction` or any of the `...CallPointer`
  variants yet — they currently fall into the `default:` case and return
  `UnsupportedOpcode` cleanly (logged, not a crash).
- **Level-load wiring** (`kisak_android_native_renderer.cpp`'s
  `StartWorldLoad` → `CompileAndRunScript`): finds exactly ONE rawfile named
  `maps/<mapName>.gsc` in the zone's already-scanned `rawFiles` (via
  `ScanZoneRawFiles`, from the original plan's Step 1), extracts its content
  string via `KisakZoneRawFile::contentOffset`/`length`, and calls
  `CompileGscSource(source)` (lex+parse+compile in one call, one file, one
  `KisakScriptProgram`). This plan's cross-file step changes this call site
  to compile a SET of files together — see Step 4.
- **Real `.gsc` corpus already on disk**: the prior plan's device-pulled
  killhouse rawfiles remain in `/tmp/claude-*/scratchpad/gsc_killhouse/` (if
  the scratchpad survived; re-pull via the `run-kisakcod-android` skill's
  `data-status`/rawfile-dump mechanism if not), and `cargoship.ff`/
  `bog_a.ff`/`hunted.ff` were pulled to
  `/tmp/claude-*/scratchpad/step10_levels/*.ff` during the prior plan's
  Step 10 — also may not survive a session boundary; re-pull via `adb pull
  /storage/emulated/0/Android/data/com.kisakcod.android/files/cod4/zone/
  english/<map>.ff` if gone. **Every step's tests must run against real
  `.gsc` text extracted from one of these real zones, not synthetic
  snippets only** — synthetic snippets are fine for isolating a single
  construct during development, but the exit criteria for the plan as a
  whole is real killhouse.gsc getting past its real `::inside_start`
  reference.
- **Run/deploy workflow**: `.claude/skills/run-kisakcod-android/driver.sh`
  (build/install/launch/logs/crashcheck) and its `SKILL.md`. Known device
  gotchas: `adb shell input tap` doesn't register in the world view (use
  `input swipe X Y X Y 100`); the prior plan's Step 10 found that even the
  documented zero-distance-swipe fire gesture became unreliable to
  reproduce via synthetic input late in a long device session — if a step's
  device pass has trouble reproducing a UI gesture, don't over-invest in
  forcing it; the `CompileAndRunScript` diagnostic logging (no gesture
  needed, fires automatically on world load) is the primary and
  sufficient verification path for everything in this plan.
- **Device**: Xiaomi `23124RA7EO` (`a38b2d7c`), Adreno 610. Screen
  auto-locks during long sessions — wake + swipe-unlock before any device
  step if touches silently stop registering.

## Architecture facts locked in by research (2026-07-21)

Source: `src/script/scr_compiler2.cpp`, `scr_vm.cpp`, `scr_main.cpp`,
`scr_yacc2.cpp`, `scr_yacc.h`, `scr_stringlist.cpp` (the reference
decompilation). **File-attribution gotcha already hit once in this
project**: `scr_yacc.cpp`/`scr_compiler.cpp` (no "2") are non-build parallel
reference dumps at different line numbers than the actual compiled files
(`scr_yacc2.cpp`/`scr_compiler2.cpp`, confirmed via
`scripts/common_files.cmake:434,456`) — cite the "2" files. Also:
`scr_yacc.cpp` specifically is flagged "Non-ISO extended-ASCII text" by
`file(1)`, so plain `grep` silently returns zero matches on it without
`-a` (`scr_compiler.cpp` is plain ASCII and greps fine — the encoding
gotcha is per-file, not a blanket property of every non-"2" twin) — a
likely cause of past misattribution, watch for this if re-deriving
anything.

1. **AST shapes** (`scr_yacc2.cpp:1447-1477`, node helpers in
   `scr_parsetree.cpp:34-100`): `ENUM_local_function` (bare `func` name, 2
   payload slots: interned name + source pos), `ENUM_far_function`
   (`path\file::func`, 3 payload slots: interned canonical filename +
   interned name + source pos), `ENUM_function` (wraps either of the above
   when the name is used as a VALUE rather than an immediate call — this is
   the mechanism behind `::inside_start` passed as a bare argument to
   `default_start(...)`), `ENUM_function_pointer` (wraps an arbitrary
   sub-expression for the DIFFERENT `[[ expr ]](...)` call-through-a-value
   syntax — not the same as bare `::func`, not needed by this plan's real
   corpus and out of scope here).
2. **Compile-time resolution, not runtime linking** (`scr_compiler2.cpp`
   `EmitFunction`, `:2765-2888`; `ScriptCompile`, `:5607-5722`): a far
   reference (`path\file::func`) triggers `AddFilePrecache` (`:555-565`,
   call site `:2808`); after the CURRENT file finishes compiling,
   `ScriptCompile` walks its precache list and calls
   `Scr_LoadScriptInternal` **recursively, synchronously** (`:5664`) for
   each referenced file — chain-loading, not deferred. A file already
   loaded this session is reused via `scrCompilePub.loadedscripts`
   (`scr_main.cpp:240`), not recompiled.
3. **Backpatch via a pending-call-site list, one embedded pointer per call
   site** (`scr_compiler2.cpp` `EmitFunction:2846-2870`, `LinkThread:2709-
   2758`): if the target isn't resolved yet, a placeholder cell is emitted
   (`EmitCodepos`, a raw pointer-sized write) holding a 0/1 scope tag, and
   the cell's OWN ADDRESS is registered against the target symbol
   (`GetNewVariable`, `:2870`). Once the target function is fully compiled
   (`LinkFile`/`LinkThread`), every pending cell for that symbol is
   overwritten in place with the real code pointer (`:2752`). **The
   compiled bytecode never distinguishes local vs. far calls — both compile
   to the identical `OP_ScriptFunctionCall`/`OP_ScriptThreadCall` with one
   embedded pointer operand.** This directly parallels the port's own
   existing `CallFixup`/`callFixups` mechanism (`kisak_script_compiler_
   android.cpp`) — same idea, needs one more level (cross-file, not just
   forward-reference-within-one-file).
4. **VM dispatch is trivial once the value exists** (`scr_vm.cpp:2310-2314`
   for `OP_GetFunction`; `:2784-2793`/`:2855-2865` for
   `OP_ScriptFunctionCall`/`OP_ScriptThreadCall`; `:2797-2814`/`:2870-2889`
   for the `...CallPointer` variants): `OP_GetFunction` just reads the
   embedded pointer and pushes it as a `VAR_FUNCTION`-typed value; the
   `...CallPointer` opcodes read the callee address from a POPPED stack
   value (type-checked to be `VAR_FUNCTION`) instead of from the
   instruction stream. `Scr_ReadCodePos` (`scr_vm.cpp:1637-1642`) is a bare
   pointer read — no hash, no lookup, at VM time.
5. **Filename/function-name interning is a compile-time-only hash**
   (`scr_stringlist.cpp:24-43` `GetHashCode`, `:228-239` `SL_GetString_`,
   `:854-860` `Scr_CreateCanonicalFilename`) — used purely to build the
   compiler's own symbol table (filename → file-object → function-object);
   the VM never sees a hash, only the final patched pointer. **No
   `Scr_GetFunctionHandle`-style function exists in this codebase** (search
   confirmed absent) — the port should not invent one; the compile-time
   symbol table described above is the actual and only mechanism.

## De-risking strategy

Split the value-representation work (function-pointer `KisakScriptValue`
variant + `OP_GetFunction` + `...CallPointer` VM support, testable entirely
within ONE file/`KisakScriptProgram`, zero cross-file complexity) from the
cross-file compilation model (chain-loading + shared symbol table +
backpatch, the genuinely novel and highest-risk part). Step 1-3 de-risk the
former with synthetic + real-single-file tests; Step 4-5 tackle the latter,
informed by exactly how retail does it (architecture facts above) rather
than improvising; Step 6 is the real-device payoff validation.

## Step graph

```
Step 1 (KisakScriptValue function-pointer      Step 2 (parser: bare ::func +
variant + OP_GetFunction)                       path\file::func(args) AST nodes)
        │                                               │
        └───────────────────┬───────────────────────────┘
                             ▼
              Step 3 (VM: ...CallPointer opcode + same-file compiler
              wiring, validated end-to-end on a same-file pointer —
              first join point, needs both 1 and 2)
                             │
                             ▼
              Step 4 (cross-file compilation model: chain-load +
              shared program + backpatch table — the big new
              subsystem)                                  [strongest]
                             │
                             ▼
              Step 5 (compiler: resolve path\file::func against
              step 4's model, wire into the real level-load call site)
                             │
                             ▼
              Step 6 (device validation against real zone-local
              namespaced calls; document the new, later failure
              point for cross-zone-only references like _blackhawk)
```

**Real parallelism**: Steps 1 and 2 touch disjoint files (`kisak_script_vm_
android.{h,cpp}` vs. `kisak_script_parser_android.{h,cpp}`) and have no
logical dependency on each other — develop them in parallel. Steps 3
onward are strictly serial: each step's compiled artifact is the input the
next step's tests exercise.

---

## Step 1 — Function-pointer value type + `OP_GetFunction`

**Depends on:** nothing
**Model tier:** default

### Context brief

`KisakScriptValue`/`KisakScriptValueType` (`kisak_script_vm_android.h`) needs
a new variant representing retail's `VAR_FUNCTION` (an entry-offset
reference usable as a first-class value — the mechanism behind
`default_start(::inside_start)`, where `::inside_start` is evaluated as a
VALUE and passed as an argument, not called immediately). This step adds
the value representation and the VM opcode that produces it
(`OP_GetFunction`) — it does NOT add parser or compiler support yet (that's
Step 2/3); test by hand-assembling bytecode the same way the original
plan's Step 3 did.

### Tasks

- [ ] Add a new `KisakScriptValueType` entry (e.g. `FunctionRef`) to
      `kisak_script_vm_android.h`, holding a `uint32_t` entry-offset payload
      (reuse the existing `i` field, or add a dedicated field — decide and
      document why; note that `CodePos`/`PreCodePos` are internal stack
      markers with no payload today, this is a genuinely new payload-bearing
      variant, not the same shape).
- [ ] Implement `OP_GetFunction` in `Interpreter::Run()`'s dispatch switch
      (`kisak_script_vm_android.cpp`): reads a 4-byte codepos operand (same
      `cursor.ReadCodePos()`/offset convention as `OP_ScriptFunctionCall`,
      confirmed in the original plan's Step 3 — do NOT reinvent a different
      operand width/endianness) and pushes a `FunctionRef` value.
- [ ] Extend `KisakScriptValue::Describe()`/`AsString()`/`Truthy()` for the
      new variant (decide sensible behavior — e.g. `Describe()` shows the
      offset, `Truthy()` is always true for a valid reference, `AsString()`
      can be an empty string or a placeholder — document the choice).
- [ ] Host-side test harness (g++, scratchpad, same pattern as every prior
      step): hand-assemble a program with two functions where one is
      referenced via a hand-emitted `OP_GetFunction` + entry offset, confirm
      the resulting stack value has the right type and offset.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build   # must compile clean on all 4 ABIs
# Host-side harness output pasted into the PR description.
```

No device test needed — inert until Step 3 calls it.

### Exit criteria

- New value variant + `OP_GetFunction` implemented and host-tested.
- Build clean on all 4 ABIs.

### Rollback

New enum entry + one new opcode case, additive to existing files — revert
freely, nothing else references the new variant yet.

---

## Step 2 — Parser: bare `::func` and `path\file::func(args)`

**Depends on:** nothing (can be developed in parallel with Step 1, though
Step 3's end-to-end test needs both)
**Model tier:** default

### Context brief

Add grammar productions for the two real syntaxes, informed by the
architecture facts above but using the port's own AST representation (a
kind-tagged struct, not retail's generic `sval_u` node system — same
deliberate departure the original plan's Step 7 already established and
justified). Per the architecture facts, retail's `ENUM_local_function` vs
`ENUM_far_function` split is about WHICH symbol table the compiler looks
in, not a different call MECHANISM — the parser's job is just to capture
"was a filename given or not," leaving resolution strategy to Step 3/5's
compiler.

**Two distinct syntactic positions, both must work:**
- As a call target directly: `maps\_blackhawk::main()`, `inside_start()`
  (already works, no filename), and — critically — a BARE reference with NO
  call parens, used as a value: `::inside_start` (no filename) and (for
  completeness, though not confirmed needed by the real corpus yet)
  `maps\_blackhawk::main` (with filename, no parens).
- Real corpus confirms the no-parens bare form is what's actually used:
  `default_start( ::inside_start )` (killhouse.gsc line 27) — a bare
  `::name` token sequence appearing in ARGUMENT position, not followed by
  `(`.

### Tasks

- [ ] Add new `KisakAstNodeKind` entries for a namespaced call and a
      function-pointer-value reference (decide naming — e.g.
      `NamespacedCallExpr` mirroring `CallExpr` but with a filename field,
      and `FunctionRefExpr` for the bare `::name` / `path\name::func`
      value-position case). Document the shape (what's in `text` vs. a new
      field vs. `children`) as clearly as the existing `KisakAstNodeKind`
      doc comments do.
- [ ] Extend `ParsePrimary` (`kisak_script_parser_android.cpp`) to
      recognize a leading `::` (bare function pointer, no filename) and an
      `Identifier` immediately followed by `\` (namespaced — the path
      itself may have multiple `\`-separated segments per the real corpus,
      e.g. `maps\createart\killhouse_art::main()` confirmed in Step 1's
      real killhouse.gsc notes — the grammar must accept 1+ segments, not
      just exactly one).
- [ ] After the path/`::` prefix, if followed immediately by `(`, parse as
      a call (reuse `ParseArgList`, same as `CallExpr`); if not, parse as a
      bare value reference (the `default_start(::inside_start)` case).
- [ ] Host-side test harness: parse real excerpts from killhouse.gsc line
      27 (`default_start( ::inside_start );`) and a namespaced call
      (`maps\_blackhawk::main( "vehicle_blackhawk" );`, killhouse.gsc line
      209) — dump the AST for both, confirm correct shape by eye. Use the
      REAL lines from the real corpus (re-pull if the scratchpad didn't
      survive — see "What already exists" above), not paraphrased
      approximations.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness: parse the two real excerpts above, dump AST, paste into PR.
```

### Exit criteria

- Both real syntaxes (bare `::func` value, `path\file::func(args)` call,
  including multi-segment paths) parse into a sensible AST with zero
  parser errors.
- Grammar boundary documented: what's still NOT handled (e.g. `[[ expr ]]`
  pointer-call syntax, confirmed out of scope per the architecture facts
  above since it's not in the real corpus).

### Rollback

New standalone AST node kinds + new `ParsePrimary` branches — revert
freely, existing grammar productions are untouched.

---

## Step 3 — VM `...CallPointer` support + same-file compiler wiring

**Depends on:** Step 1 (value type), Step 2 (AST to compile from) — first join point
**Model tier:** default

### Context brief

Wire Step 2's `FunctionRefExpr`/bare-pointer AST node through the compiler
to Step 1's `OP_GetFunction`, and implement the VM opcode that CALLS
through a function-pointer value (`OP_ScriptFunctionCallPointer` only —
NOT `OP_ScriptThreadCallPointer`: this plan's objective explicitly excludes
threading, the port has no `OP_ScriptThreadCall` dispatch to model a
pointer-call variant on top of (confirmed: it only appears in the opcode
name/`Describe` table today, never as a dispatch case — it hits `default`/
`UnsupportedOpcode`), a `thread` call is asynchronous by nature and
implementing it as a synchronous jump would be semantically wrong, and
none of this plan's real validation targets need it. Leave
`OP_ScriptThreadCallPointer` unimplemented, deferred to a future threading
blueprint. Method-pointer variants are likewise out of scope, they need
the entity model this plan explicitly excludes. This step is scoped to
SAME-FILE function pointers only (e.g. `f = ::localFunc;`, a
function pointer to something defined in the same compiled `Program`) —
cross-file (`maps\_blackhawk::main` used as a bare value, or called) is
Step 5's job, once Step 4's cross-file model exists. Real corpus check: is
`::inside_start` (killhouse.gsc line 27) same-file or cross-file? —
`inside_start` is defined in `killhouse.gsc` itself (confirm by grep before
starting), so THIS step's real-corpus validation target should be
achievable without Step 4's cross-file work yet.

### Tasks

- [ ] Implement `OP_ScriptFunctionCallPointer` only (see context brief for
      why `OP_ScriptThreadCallPointer` is explicitly excluded, not just
      forgotten) in `Interpreter::Run()` (`kisak_script_vm_android.cpp`):
      pop a value,
      require `FunctionRef` type (runtime error otherwise, matching
      retail's "is not a function pointer" per the architecture facts —
      use this project's established result-code error model, not a
      crash), jump to its offset the same way `OP_ScriptFunctionCall`
      already does.
- [ ] Extend the compiler (`kisak_script_compiler_android.cpp`) to emit
      `OP_GetFunction` + a same-`Program` function's entry offset (reusing
      the EXISTING `CallFixup`/backpatch mechanism — a `FunctionRefExpr`
      pointing at a function that isn't emitted yet needs the same
      forward-reference handling a same-file `CallExpr` already gets) when
      it sees a bare `::func` (no filename) used as a value.
- [ ] Decide and implement: when a `FunctionRefExpr` value is later CALLED
      (not just held), does that go through `OP_ScriptFunctionCallPointer`
      unconditionally, or does the compiler special-case
      "`::func` immediately followed by a call" as a direct
      `OP_ScriptFunctionCall` (skipping the pointer round-trip, an
      optimization retail doesn't need to make because its `ENUM_function`
      wrapper already separates "called directly" from "used as a value" —
      see the AST shapes in the architecture facts). Either is valid;
      document the choice.
- [ ] Host-side test: hand-write and compile (through the real pipeline
      now, not hand-assembled bytecode) a synthetic script with a same-file
      function pointer stored in a local, passed to another function, and
      called — confirm correct execution.
- [ ] Real-corpus test: extract killhouse.gsc (or re-pull if needed),
      confirm `inside_start` is defined in the same file, compile the
      REAL `default_start( ::inside_start );` line (in isolation or as
      part of a larger excerpt) and confirm the `::inside_start` reference
      itself now compiles without error (the surrounding script may still
      hit OTHER gaps — that's fine, isolate just this construct if the
      full file doesn't compile yet for unrelated reasons).

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness: synthetic function-pointer test + real killhouse.gsc
# ::inside_start excerpt, both compiling and (for the synthetic one) executing
# with correct output, pasted into PR.
```

### Exit criteria

- Same-file function pointers compile and execute correctly end-to-end
  (synthetic test).
- The real `::inside_start` construct from killhouse.gsc line 27 compiles
  without error (in isolation if the full file still hits other gaps).

### Rollback

Additive to Step 1/2's files — revert the new opcode cases and compiler
emission branch, Step 1/2's standalone additions are unaffected.

---

## Step 4 — Cross-file compilation model: chain-load + shared program + backpatch

**Depends on:** Step 3 (needs a working function-pointer/call mechanism to extend cross-file)
**Model tier:** strongest (the plan's own equivalent of the original VM
plan's Step 3/8 — highest complexity, a genuinely new subsystem, get it
reviewed carefully)

### Context brief

**Size warning, read first**: this step bundles a symbol-table redesign,
filename canonicalization, the chain-load driver loop, and the cross-file
backpatch mechanism — plausibly several PRs' worth of work even before its
own test harness. That's why it's tagged `strongest` and framed as "a
genuinely new subsystem," but if it proves too large in practice, split it
(e.g. "4a: symbol table + canonicalization, single-file behavior
unchanged" vs. "4b: chain-load driver + backpatch + the 3-file test") using
this plan's own step-graph as a guide — don't force it into one PR just
because it's numbered as one step.

This is the step that actually fixes `maps\_blackhawk::main()`-style calls
into a DIFFERENT file than the one being compiled — **though note the
Objective section's own correction: `_blackhawk` specifically is not
resolvable by this mechanism at all, since it isn't in any scanned zone;
this step's real payoff is namespaced calls whose target genuinely is
in-zone** (confirmed present in killhouse.ff: `killhouse_fx`,
`killhouse_anim`, `createart\killhouse_art`). Per the architecture facts,
retail's model is: (a) one shared compile session/program buffer across
every file loaded together, (b) chain-loading — seeing a reference to
`maps\_blackhawk.gsc` while compiling `maps\killhouse.gsc` triggers loading
and compiling `_blackhawk.gsc` right then, recursively, (c) a two-level
symbol table (filename → file-object → function-object) built as files
compile, (d) a pending-call-site backpatch list per (file,func) symbol,
resolved once that function is actually emitted.

**This port's current architecture constraint**: `.gsc` source only exists
as an in-memory string extracted from a zone's rawfile scan
(`ScanZoneRawFiles`, `kisak_zone_rawfile_android.h`) — there is no
filesystem-style "open this other .gsc file" primitive today; "loading"
`maps\_blackhawk.gsc` means finding ANOTHER entry in the SAME zone's already
-scanned `rawFiles` list (by name, exactly like `StartWorldLoad` already
finds `maps/<mapName>.gsc` — see "What already exists"). This actually
SIMPLIFIES retail's model: all of a mission zone's `.gsc` files are already
extracted together in one pass (`ScanZoneRawFiles` returns everything at
once, no incremental/lazy loading needed) — the port doesn't need real
chain-loading AT LOAD TIME, it can compile every `.gsc` rawfile in the zone
together in one shot. **Decide and document explicitly**: does this step
(a) compile ALL `.gsc` rawfiles found in a zone into one shared
`KisakScriptProgram` regardless of whether they're actually referenced
(simpler, wastes some compile time on unreferenced files), or (b) do real
lazy chain-loading matching retail exactly (compile the entry file, and
only compile a referenced file when a `path\file::func` is actually seen,
recursively) — given the port's data is already fully in memory, (a) is
almost certainly the pragmatic choice for this port, but state the
reasoning, don't silently assume it.

### Tasks

- [ ] Design a `KisakScriptCrossFileCompileResult` (or similar) that wraps
      ONE shared `KisakScriptProgram` plus a qualified symbol table:
      `(canonical-filename, function-name) -> entry-offset`, extending
      `KisakScriptProgram::functionEntryPoints` (currently a flat
      `unordered_map<string, uint32_t>` keyed by bare function name — decide
      whether to key this new cross-file table by a combined
      `"path\to\file::funcname"` string, or a nested map; document the
      choice and update/extend `KisakScriptProgram` accordingly without
      breaking Step 3/prior steps' same-file lookups).
- [ ] Implement filename canonicalization matching real `.gsc` reference
      syntax (`maps\_blackhawk` -> the rawfile name `maps/_blackhawk.gsc` —
      note the real corpus uses `\` as the path separator in source but
      rawfile names use `/`, confirm this exact mapping against real
      rawfile names already dumped in the original plan's Step 1 work
      before assuming a naive string replace is correct).
- [ ] Implement the compile-all-referenced-files loop: compile the entry
      file first, collect every `NamespacedCallExpr`/cross-file
      `FunctionRefExpr` filename seen (a "precache list," mirroring
      retail's `AddFilePrecache`), then for each one not yet compiled, find
      its rawfile by name in the same zone's `rawFiles`, extract its
      source, and compile it into the SAME shared `KisakScriptProgram`
      (function offsets must not collide — bytecode from different files
      is just appended, offsets are absolute into the one shared buffer,
      matching how same-file compilation already works). Missing file =
      compile error (matching retail's `CompileError("Could not find
      script '%s'")`), not a silent skip.
- [ ] Implement the cross-file backpatch: any `NamespacedCallExpr`/
      cross-file `FunctionRefExpr` compiled BEFORE its target file has been
      compiled needs the same kind of pending-fixup entry the existing
      `CallFixup` list already provides for same-file forward references —
      extend that mechanism (or add a parallel one) to also carry the
      target's (filename, funcname) so it can be resolved once that file's
      compilation adds its entry to the qualified symbol table.
- [ ] Host-side test: 3 synthetic in-memory "files" (as separate strings,
      simulating separate rawfiles) where file A calls into file B which
      calls into file C, including at least one FORWARD reference (A
      references a function in B that's compiled after A in whatever order
      the driving code processes the precache list) — confirm the shared
      program compiles and executes correctly across all three.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
# Host-side harness: 3-file cross-reference test (including a forward
# reference), full compile + execute, correct output pasted into PR.
```

### Exit criteria

- Multi-file compilation into one shared program works for the 3-file
  synthetic test, including a forward cross-file reference.
- Explicit design note on chain-load-eagerly-vs-lazily and the filename
  canonicalization (`\` source syntax -> `/` rawfile name) is in the PR
  description.

### Rollback

New standalone types/functions; if `KisakScriptProgram` itself needed a
field addition for the qualified symbol table, that's additive (a new
field, not a changed one) — revert the new compile-all-files driver
function and any additive `KisakScriptProgram` field; same-file compilation
(Step 1-3, and the entire prior VM plan) is unaffected as long as the new
field defaults empty/unused for single-file callers.

---

## Step 5 — Wire cross-file resolution into the compiler + real level load

**Depends on:** Step 4
**Model tier:** default

### Context brief

Step 4 built the cross-file compile-and-link mechanism as a standalone,
testable unit (per this plan's own established "don't wire into the real
call site until it's proven" pattern, matching how the original VM plan's
Step 5 kept its entity module standalone until Step 9). This step does two
things: (a) makes `kisak_script_compiler_android.cpp`'s expression emitter
actually USE Step 4's cross-file table when it sees a `NamespacedCallExpr`/
cross-file `FunctionRefExpr` (today `EmitCall` only checks builtins + same-
`Program` functions — extend it to also consult the cross-file symbol table
when a filename is present), and (b) updates the real level-load call site
(`StartWorldLoad`/`CompileAndRunScript`, `kisak_android_native_renderer.cpp`)
to invoke Step 4's compile-all-referenced-files path instead of
`CompileGscSource`'s single-file path.

### Tasks

- [ ] Extend the compiler's call-resolution order (documented already in
      `kisak_script_compiler_android.h`'s header comment — update that
      comment too) to: builtin table → same-file function → **cross-file
      (filename, funcname) table (new)** → compile error. Same-file lookup
      must still take priority for an unqualified name (a bare `func()`
      call with no filename never triggers cross-file lookup, matching
      retail's `ENUM_local_function` vs `ENUM_far_function` split being a
      SYNTACTIC distinction, not a fallback search order).
- [ ] Update `StartWorldLoad`'s `CompileAndRunScript` call site (or add a
      new entry point alongside it — decide and document) to pass the
      zone's full `rawFiles` list + `zoneData` (already in scope at that
      call site, per the original plan's Step 9 work) so Step 4's compiler
      can look up referenced files by name, instead of only ever compiling
      the single `maps/<mapName>.gsc` string.
- [ ] Keep the existing diagnostic logging style (step 9's `"Step9 script
      '%s': ..."` pattern) but extend it to also report which additional
      files were chain-compiled and how many cross-file calls resolved
      (e.g. `"chain-compiled 6 additional files, 12 cross-file calls
      resolved"`) — this is valuable diagnostic signal for Step 6's
      real-device validation and any future gap-hunting.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh build
.claude/skills/run-kisakcod-android/driver.sh install a38b2d7c
.claude/skills/run-kisakcod-android/driver.sh launch a38b2d7c
# Host-side harness first, then a real device pass loading killhouse
# (New Game -> Prologue -> F.N.G. -> Recruit, per this session's established
# navigation) and checking:
adb -s a38b2d7c logcat -d -s KisakCODAndroid:* | grep -i "step9\|script\|chain"
adb -s a38b2d7c logcat -d -b crash | tail -20   # must be empty
```

### Exit criteria

- Cross-file resolution wired into the real compiler emission path,
  same-file-first priority preserved.
- Real level-load call site updated and building clean on all 4 ABIs.

### Rollback

If device validation destabilizes the app, reverting this step's call-site
change (back to single-file `CompileGscSource`) is a one-function-call
revert — Step 4's cross-file module stays intact and testable in isolation
regardless.

---

## Step 6 — Device validation: real killhouse past `::inside_start`

**Depends on:** Step 5
**Model tier:** default

### Context brief

The payoff step. No new subsystem code — validation and honest
documentation, matching the original VM plan's own Step 10 pattern.
Confirm the REAL killhouse.gsc (device-extracted, not a trimmed excerpt)
now gets past line 27's `default_start( ::inside_start );` (a same-file
function pointer — fully resolvable) and its in-zone namespaced calls
(`killhouse_fx`/`killhouse_anim`/`createart\killhouse_art`, confirmed
present in killhouse.ff's own rawfiles), and document exactly how much
further it compiles before hitting the NEXT gap.

**Do not expect `maps\_blackhawk::main()` (line 209) to resolve** — this
plan's own Objective section documents why: `_blackhawk`/`_load`/
`_80s_hatch1`/`_bus`/`_bm21_troops`/`_humvee`/`_small_wagon` (the vehicle-
init calls at lines 209-219) are shared cross-mission scripts that are
**not present in killhouse.ff or code_post_gfx.ff at all** — this port has
no mechanism to load a `.gsc` from outside the currently-scanned zone. The
correct, successful outcome for this step is: the compile failure point
moves from line 27 (a PARSE error, pre-this-plan) to line 209 (a
MISSING-FILE COMPILE error, post-this-plan) — genuine progress (182 lines
further, and a category change from "can't even parse this" to "parses and
resolves everything except data this port doesn't have access to"), NOT
full end-to-end compilation of killhouse.gsc's `main()`. Report it that
way; do not claim more than this.

### Tasks

- [ ] Run the full updated pipeline against real killhouse.gsc on device,
      confirm no crash, and capture the exact new failure point (line
      number + construct). Expected: line 209's `maps\_blackhawk::main()`,
      now failing with a specific "file not found" compile error rather
      than line 27's parse error — this IS the success case for this step,
      per the context brief above, not a shortfall.
- [ ] Re-run the same host-side multi-level check the original plan's Step
      10 did (cargoship/bog_a/hunted, pulled `.ff` files, same host
      fastfile/rawfile/compiler pipeline) and document the new failure
      point for each. bog_a's line 43 `maps\bog_a_fx::main();` is exactly
      this plan's target construct — `bog_a_fx` is a per-mission fx script
      (same naming pattern as killhouse's own `killhouse_fx`, which IS
      in-zone), so it's plausible this one resolves and bog_a.gsc compiles
      further than killhouse.gsc does — but confirm by actually checking
      bog_a.ff's rawfile list for `maps/bog_a_fx.gsc` before assuming it's
      present; don't repeat this plan's own original mistake of assuming a
      referenced file is in-zone without checking. cargoship (array
      subscript, line 10) and hunted (`#`-directive, line 5) are NOT
      expected to move at all — both fail on a different, still-out-of-
      scope gap before any `::` reference would be reached; confirm they
      still fail at the SAME line/construct as before (a regression if
      they now fail earlier, informative if unchanged).
- [ ] Confirm no regressions in the 5 prior gameplay milestones (viewmodel,
      hitscan, map_ents props, audio, HUD stick) — manual device pass,
      matching the original plan's Step 10 tasks. If synthetic-input
      reproduction of fire/audio proves unreliable again (a real
      possibility per the original plan's own Step 10 finding), document
      that honestly rather than claiming a false pass — it's not blocking
      since this plan's commits don't touch touch/render/audio code either.
- [ ] Update `plans/gscript-vm-gaps.md` with this plan's results: mark
      namespaced calls/function pointers as covered, update the "real
      multi-level validation" table with the new failure points, and
      re-rank the recommendation list if warranted.
- [ ] Update the project's auto-memory (`project_kisakcod_android.md`)
      with this milestone, following the exact structure every prior
      milestone used.

### Verification

```bash
cd /home/jacques/Projects/KisakCOD
.claude/skills/run-kisakcod-android/driver.sh run a38b2d7c
# Manual device pass per the tasks above — graded on documentation quality
# and the absence of regressions/crashes, not new automated checks.
```

### Exit criteria

- Real killhouse.gsc's compile failure point has moved from line 27 to
  line 209 (the expected, honest outcome — see context brief), or further
  if `_blackhawk` resolution turns out to be achievable by some means not
  anticipated here; either way, document exactly where it lands and why.
- `plans/gscript-vm-gaps.md` and auto-memory updated with concrete new
  findings.
- No regressions in the 5 prior gameplay milestones (or honestly flagged
  as inconclusive, matching the original plan's own precedent, if synthetic
  input reproduction fails again).

### Rollback

Documentation-only step; if a regression is FOUND, that's a bug in an
earlier step of THIS plan, not this step — file it against the responsible
step and fix there.

---

## Plan-level notes for whoever executes this

- **Do not attempt entity field access, arrays, `switch`, or threading**
  even if a real script's next failure point (Step 6) makes one of them
  look tractable — those are separate blueprints, per `gscript-vm-gaps.md`'s
  own recommended order (this plan first, then arrays, then the entity
  model, then threading).
- **Do not attempt the world-geometry culling fix** or otherwise touch
  rendering/touch/audio code — genuinely unrelated to this plan, already
  investigated and reverted once in the original VM plan's session.
- Every step reuses the established host-then-device workflow: prototype
  in `/tmp/claude-*/scratchpad` with `g++` before touching the NDK build.
- If the prior plan's scratchpad `.gsc`/`.ff` files didn't survive a
  session boundary, re-derive them: `adb pull` the real `.ff` files (see
  "What already exists" for exact paths) rather than working from
  paraphrased/remembered excerpts — every exit criterion in this plan
  depends on REAL source text, not approximations.
