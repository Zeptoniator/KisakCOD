#pragma once

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
