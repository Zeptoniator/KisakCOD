#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "kisak_script_parser_android.h"
#include "kisak_script_vm_android.h"

// GScript compiler: step 7's AST -> step 2/3/4's bytecode
// (KisakScriptProgram) — blueprint plans/android-gscript-vm-port.md, step 8.
// This is the first hard join point: it consumes the parser's AST (step 7)
// and emits ONLY the opcode subset step 3's VM dispatch loop and step 4's
// builtin table actually implement.
//
// Reference (spirit, NOT a byte-for-byte port): scr_compiler.cpp /
// scr_compiler2.cpp's EmitOpcode/EmitExpression/EmitStatement/EmitByte/
// EmitShort/EmitFloat/Scr_FindLocalVarIndex. Retail emits into the global
// scrVarPub.programBuffer against retail's own 138-opcode surface and its
// SL_* intern table; this emitter targets OUR trimmed KisakScriptProgram
// (kisak_script_vm_android.h) instead, so opcode selection, the string pool,
// and the builtin-index space are all this port's own (KisakScriptBuiltinTable
// indices, not retail's functions[251]/methods[] numbering).
//
// Deliberate, DOCUMENTED scope boundary (not a bug — mirrors the parser's own
// "grammar parses it, semantics are out of scope" pattern): the current VM has
// NO entity/object model — no OP_GetSelf/GetLevel/GetGame, no
// OP_Eval*FieldVariable, no OP_ScriptMethodCall/OP_CallBuiltinMethod handling.
// So this compiler REJECTS, with a specific compile error, any construct that
// would need one: field access/assignment (`self.voice = ...`), method-call
// syntax (`self setModel(...)`), and bare references to self/level/game/anim.
// Building that object model is a genuinely new subsystem (step 9+), not a
// small opcode gap to patch into step 3 here.
//
// Call-name resolution order (namespaced-calls blueprint, steps 3-5), overall
// across the whole compiler — NOT a single per-call-site search order, since
// which tier applies is a SYNTACTIC distinction (does the AST node carry a
// path?), matching retail's ENUM_local_function/ENUM_function vs
// ENUM_far_function split:
//   1. Builtin table (KisakScriptFindBuiltinIndex) — bareword calls only.
//   2. Same-file function (this Program's functionEntryPoints) — bareword.
//   3. Local variable holding a FunctionRef (step 3's deliberate simplification
//      beyond real GSC) — bareword.
//   4. Cross-file (canonical-file, funcname) table (step 4's
//      qualifiedFunctionEntryPoints, step 5 wires it into the real level-load
//      path) — ONLY for a NamespacedCallExpr/FunctionRefExpr node with a
//      non-empty path; a bareword call never falls through to this tier, and
//      a namespaced (non-empty-path) node never falls through to tiers 1-3.
//   Anything else: compile error, not a silent no-op.

// Result envelope — matches the plain result-struct error model established by
// step 3 (VM), step 6 (lexer) and step 7 (parser): no exceptions, no longjmp.
// `errors` empty means success; on failure `program` holds whatever partial
// bytecode was emitted before the error and must be ignored by the caller.
struct KisakScriptCompileResult {
    KisakScriptProgram program;
    std::vector<std::string> errors;  // "function X, line N: message"; empty == ok
};

// Compile a whole parsed program (the AST root MUST be a Program node whose
// children are FunctionDef nodes — exactly what ParseGscSource produces).
KisakScriptCompileResult CompileGscAst(const KisakAstNode& program);

// Convenience: lex + parse + compile in one call (the host test harness and
// step 9's level-load wiring both want the whole pipeline behind one call).
// Parser errors are surfaced through the same `errors` list, prefixed "parse:".
KisakScriptCompileResult CompileGscSource(const std::string& source);

// ---------------------------------------------------------------------------
// Namespaced-calls blueprint (plans/android-gscript-namespaced-calls.md),
// step 4: cross-file compilation into ONE shared KisakScriptProgram.
//
// Retail chain-loads a referenced .gsc off the filesystem the moment it sees a
// `path\file::func` reference (scr_compiler2.cpp AddFilePrecache/ScriptCompile/
// Scr_LoadScriptInternal). This port has no filesystem .gsc loading at all —
// .gsc source only ever exists as an in-memory string already sliced out of a
// zone's ScanZoneRawFiles() pass, which returns EVERY rawfile in one shot. So
// this driver does NOT do retail's lazy per-reference chain-load: it compiles
// the entry file, collects the files its cross-file references name (a
// "precache list", mirroring AddFilePrecache), and compiles each referenced
// file into the SAME program, repeating until the reference set closes — eager
// over the transitive closure of what is actually referenced, not lazy, and
// not "every rawfile in the zone" (unreferenced files cost nothing and may not
// even be valid in this trimmed grammar). Forward references (file A naming a
// function in file B compiled after A) are resolved by a cross-file backpatch
// pass after every file is emitted — the same idea as the single-file
// CallFixup mechanism, one level up (per (file,func) instead of per func).

// How the driver obtains a file's source by its CANONICAL name (forward-slash
// path + ".gsc", e.g. "maps/killhouse_fx.gsc" — the real rawfile-name form;
// note real .gsc SOURCE writes the SAME path with backslashes,
// `maps\killhouse_fx::main`, but zone rawfile names use "/"). Returns nullopt
// when no such file exists in the available set (=> a specific compile error,
// matching retail's CompileError("Could not find script '%s'")). Designed as a
// std::function so step 5 can plug in a lambda over the zone's rawFiles list
// without this step depending on any zone/JNI code; host tests pass a lambda
// over a std::unordered_map<canonical-name, source>.
using KisakScriptFileLoader =
    std::function<std::optional<std::string>(const std::string& canonicalName)>;

// Output of a cross-file compile. `program` holds the one shared bytecode
// buffer with every compiled file's functions appended into it (offsets are
// absolute into this buffer). Same result-struct/no-exceptions error model as
// KisakScriptCompileResult; `errors` empty means success. The entry point is
// NOT read from program.functionEntryPoints (bare names clobber across files —
// see KisakScriptProgram): look the entry file's function up in
// program.qualifiedFunctionEntryPoints via the `entryCanonical` key, e.g.
// program.qualifiedFunctionEntryPoints.at(result.entryCanonical + "::main").
struct KisakScriptCrossFileCompileResult {
    KisakScriptProgram program;
    std::vector<std::string> errors;
    std::string entryCanonical;  // canonical name the compile started from
    // Every canonical file actually compiled, in discovery order, entry file
    // first. Diagnostic only (namespaced-calls blueprint step 5's real-device
    // logging: "chain-compiled N additional files") — size()-1 is the count of
    // files pulled in beyond the entry file, since entry is always [0].
    std::vector<std::string> compiledFiles;
    // Count of CrossFileFixup entries that resolved successfully (i.e. found
    // in program.qualifiedFunctionEntryPoints at backpatch time). Does not
    // include fixups that produced an "undefined function" error.
    size_t crossFileCallsResolved = 0;
};

// Compile `entryCanonicalName` (e.g. "maps/killhouse.gsc") and the transitive
// closure of the files its cross-file references name, obtaining each file's
// source via `loadFile`, into one shared program. A referenced file that
// `loadFile` cannot supply, and a cross-file reference to a function no
// compiled file defines, are both compile errors (not crashes, not silent
// skips).
KisakScriptCrossFileCompileResult CompileGscZoneEntryPoint(
    const std::string& entryCanonicalName, const KisakScriptFileLoader& loadFile);
