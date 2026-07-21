#include "kisak_script_compiler_android.h"

#include <cstring>
#include <unordered_map>
#include <unordered_set>

// See the header for the overall design and the deliberate entity/field-access
// scope boundary. This file is the emitter itself.
//
// Two structural decisions worth stating up front because the rest of the file
// leans on them:
//
// 1. LOCAL-VARIABLE SLOTS ARE FIXED PER FUNCTION. The VM addresses locals
//    newest-first (OP_EvalLocalVariableCached0 = the most recently created
//    local; cached index N = locals[count-1-N], see Interpreter::SlotIndex in
//    kisak_script_vm_android.cpp). If locals were created lazily mid-body, a
//    given variable's cached index would SHIFT every time a later local was
//    created — brittle to emit and review. Instead this compiler pre-scans each
//    function for every variable it assigns, creates ALL of them in the
//    prologue, and leaves the local count constant for the whole body. Each
//    variable then has a CONSTANT cached index (totalLocals-1-creationOrder),
//    which is what makes single-pass emission of reads/writes correct. This is
//    the analogue of retail's Scr_FindLocalVarIndex, adapted to the fixed-frame
//    shape our trimmed VM makes convenient.
//
// 2. BUILTIN-VS-SCRIPT-CALL RESOLUTION IS NAME-FIRST, BUILTINS WIN. A call name
//    is resolved against KisakScriptFindBuiltinIndex FIRST; if it matches, it
//    compiles to an OP_CallBuiltin* through OUR 11-entry table. Otherwise, if
//    the name is a function defined elsewhere in the SAME program, it compiles
//    to an OP_ScriptFunctionCall whose 4-byte codepos operand is backpatched
//    once every function's entry offset is known (functions can be
//    forward-referenced). Otherwise (namespaced-calls blueprint step 3), if
//    the name is a currently-declared LOCAL variable, it may hold a FunctionRef
//    at runtime (assigned from a bare `::func` reference, step 2) — compiled
//    to OP_ScriptFunctionCallPointer, which reads its target from a popped
//    stack value instead of an embedded operand. This is a deliberate,
//    documented SIMPLIFICATION beyond real GSC: retail only allows calling
//    through an arbitrary value via the separate `[[ expr ]](...)` syntax
//    (confirmed out of scope — not in the real corpus), never via bare
//    `name()`; this port instead lets bare `name()` resolve to a local
//    holding a function pointer as a third tier, so a stored `::func`
//    reference can actually be invoked without implementing bracket-pointer
//    syntax. Anything else is a compile error — there is no cross-file
//    linker in this step, so an unresolved name is genuinely uncompilable,
//    not something to invent a stub for.
//
// 3. NamespacedCallExpr/FunctionRefExpr WITH AN EMPTY PATH (step 2's bare
//    `::func`/`::func(...)` forms, no filename prefix) are same-file by
//    definition and resolve via the SAME tiers as an ordinary CallExpr/
//    IdentifierExpr — see EmitNamespacedCall/EmitFunctionRef below. A
//    NON-empty path is cross-file: resolvable ONLY inside a cross-file
//    compile session (CompileGscZoneEntryPoint, step 4), where the target
//    file's functions get registered into program.qualifiedFunctionEntryPoints
//    and a cross-file backpatch (parallel to CallFixup, keyed by
//    (canonical-file, funcname)) links the call site once that file is
//    emitted. Outside such a session (the plain single-file CompileGscAst/
//    CompileGscSource path, which has no other files to look in) a non-empty
//    path is still a specific compile error, not a silent misresolution.

namespace {

using Kind = KisakAstNodeKind;
using Op = KisakScriptOpcode;

// self/level/game/anim as bare value references have no OP_GetSelf/GetLevel/...
// handler in the VM (they hit the dispatch loop's default: case). The parser
// lowers them to IdentifierExpr with these exact names, so the compiler detects
// them by name and rejects with an entity-model-specific message.
bool IsEntityKeyword(const std::string& name) {
    return name == "self" || name == "level" || name == "game" || name == "anim";
}

const char* kEntityDeferred =
    "no self/level/game object model exists in this VM yet — deferred subsystem "
    "(step 9+), see step 8 findings";

struct FunctionLocals {
    // variable name -> creation order (0 = first local pushed into the frame).
    std::unordered_map<std::string, int> creationOrder;
    int total = 0;  // params + discovered body locals

    bool Has(const std::string& name) const {
        return creationOrder.find(name) != creationOrder.end();
    }
    // Constant per-function cached index (newest-first): see decision #1.
    int Cached(const std::string& name) const {
        return total - 1 - creationOrder.at(name);
    }
};

// A pending OP_ScriptFunctionCall/OP_GetFunction codepos operand: patch `at`
// (4 bytes) with the callee's entry offset once all functions are emitted.
// SAME-FILE only — the callee is a bare name resolved against this file's own
// functionEntryPoints at the end of this file's compile pass.
struct CallFixup {
    size_t at;
    std::string callee;
    std::string inFunction;
    uint32_t line;
};

// The cross-file analogue of CallFixup (step 4): a pending codepos operand
// whose target lives in a DIFFERENT file, identified by its fully-qualified
// "canonical/path.gsc::funcname" key. Resolved against the shared program's
// qualifiedFunctionEntryPoints only after EVERY file in the session is emitted,
// so a forward reference (file A -> a function in file B compiled after A)
// links correctly. The emitted bytecode is byte-identical to a same-file call
// (OP_ScriptFunctionCall/OP_GetFunction + a 4-byte absolute offset) — matching
// retail, where local and far calls compile to the same opcode and only the
// compile-time symbol lookup differs.
struct CrossFileFixup {
    size_t at;
    std::string qualified;   // "canonical/path.gsc::funcname"
    std::string inFile;      // canonical name of the referencing file
    std::string inFunction;
    uint32_t line;
};

struct Compiler {
    // Reference (not owned) so several per-file Compiler instances can emit
    // into ONE shared program during a cross-file session — bytecode is
    // appended, offsets stay absolute, the string pool and both symbol tables
    // are shared. Single-file callers pass a program they own locally.
    KisakScriptProgram& program;
    std::vector<std::string> errors;

    // All function names in the program (populated in pre-scan) — lets a call
    // resolve a forward-referenced same-file function before its body is
    // emitted. Per-file (a fresh Compiler per file), NOT shared.
    std::unordered_set<std::string> functionNames;
    std::vector<CallFixup> callFixups;

    // Cross-file session state; all null in a single-file compile. When set,
    // this file's functions are ALSO registered under their qualified key, and
    // a NON-empty-path reference emits a cross-file call + a CrossFileFixup
    // (into the shared list) + a precache entry, instead of erroring.
    const std::string* curCanonical = nullptr;      // this file's canonical name
    std::vector<CrossFileFixup>* crossFixups = nullptr;  // shared across files
    std::vector<std::string>* precache = nullptr;        // referenced files, this file only

    explicit Compiler(KisakScriptProgram& p) : program(p) {}

    // Per-function emit state.
    const std::string* curFunction = nullptr;
    FunctionLocals locals;

    // ---- low-level byte emission ----
    std::vector<uint8_t>& code() { return program.bytecode; }
    size_t here() const { return program.bytecode.size(); }

    void EmitByte(uint8_t b) { program.bytecode.push_back(b); }
    void EmitOp(Op op) { EmitByte(static_cast<uint8_t>(op)); }
    void EmitU16(uint16_t v) {
        uint8_t buf[2];
        std::memcpy(buf, &v, 2);
        program.bytecode.insert(program.bytecode.end(), buf, buf + 2);
    }
    void EmitI32(int32_t v) {
        uint8_t buf[4];
        std::memcpy(buf, &v, 4);
        program.bytecode.insert(program.bytecode.end(), buf, buf + 4);
    }
    void EmitU32(uint32_t v) {
        uint8_t buf[4];
        std::memcpy(buf, &v, 4);
        program.bytecode.insert(program.bytecode.end(), buf, buf + 4);
    }
    void EmitF32(float v) {
        uint8_t buf[4];
        std::memcpy(buf, &v, 4);
        program.bytecode.insert(program.bytecode.end(), buf, buf + 4);
    }
    void PatchU16(size_t at, uint16_t v) { std::memcpy(&program.bytecode[at], &v, 2); }
    void PatchU32(size_t at, uint32_t v) { std::memcpy(&program.bytecode[at], &v, 4); }

    void Error(uint32_t line, const std::string& msg) {
        std::string where = curFunction ? ("function " + *curFunction) : "top level";
        errors.push_back(where + ", line " + std::to_string(line) + ": " + msg);
    }

    // Intern a string literal into the program's pool (dedup); returns the
    // 2-byte id OP_GetString indexes by. Mirrors the test-assembler getString()
    // helper step 4 established; the pool is our stand-in for retail's SL_*
    // intern table.
    uint16_t InternString(const std::string& s) {
        for (size_t i = 0; i < program.stringPool.size(); ++i) {
            if (program.stringPool[i] == s) return static_cast<uint16_t>(i);
        }
        program.stringPool.push_back(s);
        return static_cast<uint16_t>(program.stringPool.size() - 1);
    }

    // ---- forward-jump backpatch helpers ----
    // OP_JumpOnFalse's uint16 operand is relative to just-AFTER the operand
    // (VM: `cursor.pos += off`, after ReadUnsignedShort). Emit a placeholder,
    // return the operand's byte position, patch when the target is known.
    size_t EmitJumpOnFalsePlaceholder() {
        EmitOp(Op::OP_JumpOnFalse);
        size_t at = here();
        EmitU16(0);
        return at;
    }
    // OP_jump's int32 operand is likewise relative to just-after the operand.
    size_t EmitJumpPlaceholder() {
        EmitOp(Op::OP_jump);
        size_t at = here();
        EmitI32(0);
        return at;
    }
    void PatchForward16(size_t operandAt, uint32_t line) {
        long off = static_cast<long>(here()) - static_cast<long>(operandAt + 2);
        if (off < 0 || off > 0xFFFF) {
            Error(line, "forward jump out of uint16 range (" + std::to_string(off) + ")");
            return;
        }
        PatchU16(operandAt, static_cast<uint16_t>(off));
    }
    void PatchForward32(size_t operandAt) {
        int32_t off = static_cast<int32_t>(static_cast<long>(here()) -
                                           static_cast<long>(operandAt + 4));
        PatchU32(operandAt, static_cast<uint32_t>(off));
    }
    // OP_jumpback's uint16 operand: VM does `cursor.pos -= off` after reading
    // it, so off = (position after operand) - target.
    void EmitJumpBackTo(size_t target, uint32_t line) {
        EmitOp(Op::OP_jumpback);
        size_t operandAt = here();
        EmitU16(0);
        long off = static_cast<long>(operandAt + 2) - static_cast<long>(target);
        if (off < 0 || off > 0xFFFF) {
            Error(line, "backward jump out of uint16 range (" + std::to_string(off) + ")");
            return;
        }
        PatchU16(operandAt, static_cast<uint16_t>(off));
    }

    // ---- local-variable eval / assign ----
    void EmitEvalLocal(int cached) {
        if (cached >= 0 && cached <= 5) {
            EmitOp(static_cast<Op>(static_cast<uint8_t>(Op::OP_EvalLocalVariableCached0) +
                                   static_cast<uint8_t>(cached)));
        } else {
            EmitOp(Op::OP_EvalLocalVariableCached);
            EmitByte(static_cast<uint8_t>(cached));
        }
    }
    void EmitSetLocal(int cached) {
        if (cached == 0) {
            EmitOp(Op::OP_SetLocalVariableFieldCached0);
        } else {
            EmitOp(Op::OP_SetLocalVariableFieldCached);
            EmitByte(static_cast<uint8_t>(cached));
        }
    }
    void EmitRefLocal(int cached) {
        if (cached == 0) {
            EmitOp(Op::OP_EvalLocalVariableRefCached0);
        } else {
            EmitOp(Op::OP_EvalLocalVariableRefCached);
            EmitByte(static_cast<uint8_t>(cached));
        }
    }

    // ---- pre-scan: discover every local a function assigns to ----
    // Walks the body in source (pre-)order; any Assignment whose target is a
    // plain IdentifierExpr (not an entity keyword, not already a param/known
    // local) declares a new local. Field-access targets are left alone here —
    // they're rejected at emit time with the entity-model message.
    void CollectLocals(const KisakAstNode& node, FunctionLocals& out,
                       std::vector<std::string>& order) {
        if (node.kind == Kind::Assignment && !node.children.empty() &&
            node.children[0] && node.children[0]->kind == Kind::IdentifierExpr) {
            const std::string& name = node.children[0]->text;
            if (!IsEntityKeyword(name) && !out.Has(name)) {
                out.creationOrder[name] = static_cast<int>(order.size()) + out.total;
                order.push_back(name);
            }
        }
        for (const auto& child : node.children) {
            if (child) CollectLocals(*child, out, order);
        }
    }

    // ---- expression emission ----
    // Returns true if the expression left exactly one value on the value stack.
    // Only postfix ++/-- (which lower to OP_inc/OP_dec, no stack result) return
    // false — the parser only ever produces those at statement/for-clause
    // position, never nested inside another expression, so this is safe.
    bool EmitExpression(const KisakAstNode& n) {
        switch (n.kind) {
            case Kind::IntLiteralExpr: EmitIntLiteral(n.intValue); return true;
            case Kind::FloatLiteralExpr: EmitOp(Op::OP_GetFloat); EmitF32(n.floatValue); return true;
            case Kind::BoolLiteralExpr:
                if (n.intValue) { EmitOp(Op::OP_GetByte); EmitByte(1); }
                else { EmitOp(Op::OP_GetZero); }
                return true;
            case Kind::UndefinedLiteralExpr: EmitOp(Op::OP_GetUndefined); return true;
            case Kind::StringLiteralExpr:
            case Kind::IStringLiteralExpr:
                // The VM implements only OP_GetString (a stringPool index); it
                // has no OP_GetIString handler. A localized `&"KEY"` therefore
                // degrades to a plain interned string here — the honest lowering
                // given no localization table exists, and enough to stay
                // runnable. Noted rather than silently identical.
                EmitOp(Op::OP_GetString);
                EmitU16(InternString(n.text));
                return true;
            case Kind::IdentifierExpr: return EmitIdentifierRead(n);
            // Arrays blueprint (plans/android-gscript-arrays.md) step 3, READ
            // context (an ArrayIndexExpr used as an assignment TARGET never
            // reaches here — EmitAssignment special-cases it before dispatching
            // to EmitExpression).
            case Kind::ArrayLiteralExpr: EmitOp(Op::OP_EmptyArray); return true;
            case Kind::ArrayIndexExpr:
                if (n.children.size() != 2 || !n.children[0] || !n.children[1]) {
                    Error(n.line, "malformed array subscript"); return true;
                }
                EmitExpression(*n.children[0]);   // base -> array on stack
                EmitExpression(*n.children[1]);   // key on top
                EmitOp(Op::OP_EvalArray);
                return true;
            case Kind::ArraySizeExpr:
                if (n.children.empty() || !n.children[0]) {
                    Error(n.line, "malformed .size expression"); return true;
                }
                EmitExpression(*n.children[0]);
                EmitOp(Op::OP_size);
                return true;
            case Kind::BinaryExpr: return EmitBinary(n);
            case Kind::UnaryExpr: return EmitUnary(n);
            case Kind::CallExpr: return EmitCall(n);
            case Kind::NamespacedCallExpr: return EmitNamespacedCall(n);
            case Kind::FunctionRefExpr: return EmitFunctionRef(n);
            case Kind::MethodCallExpr:
                Error(n.line, "method call '" + n.text + "' needs an entity/object model: " +
                              std::string(kEntityDeferred));
                return true;
            case Kind::FieldAccessExpr:
                Error(n.line, "field access '." + n.text + "' needs an entity/object model: " +
                              std::string(kEntityDeferred));
                return true;
            default:
                Error(n.line, "cannot compile expression node " +
                              DescribeAstNodeKind(n.kind));
                return true;
        }
    }

    void EmitIntLiteral(int32_t v) {
        if (v == 0) { EmitOp(Op::OP_GetZero); }
        else if (v > 0 && v <= 0xFF) { EmitOp(Op::OP_GetByte); EmitByte(static_cast<uint8_t>(v)); }
        else if (v < 0 && v >= -0xFF) { EmitOp(Op::OP_GetNegByte); EmitByte(static_cast<uint8_t>(-v)); }
        else if (v > 0 && v <= 0xFFFF) { EmitOp(Op::OP_GetUnsignedShort); EmitU16(static_cast<uint16_t>(v)); }
        else if (v < 0 && v >= -0xFFFF) { EmitOp(Op::OP_GetNegUnsignedShort); EmitU16(static_cast<uint16_t>(-v)); }
        else { EmitOp(Op::OP_GetInteger); EmitI32(v); }
    }

    bool EmitIdentifierRead(const KisakAstNode& n) {
        if (IsEntityKeyword(n.text)) {
            Error(n.line, "reference to '" + n.text + "': " + std::string(kEntityDeferred));
            return true;
        }
        if (!locals.Has(n.text)) {
            Error(n.line, "use of undefined variable '" + n.text +
                          "' (this subset only tracks assigned locals and parameters)");
            return true;
        }
        EmitEvalLocal(locals.Cached(n.text));
        return true;
    }

    bool EmitBinary(const KisakAstNode& n) {
        if (n.children.size() != 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed binary expression"); return true;
        }
        // Short-circuit && / || have no direct opcode; lower to the VM's
        // OP_JumpOnFalseExpr / OP_JumpOnTrueExpr expression jumps (which keep or
        // drop the left operand instead of always popping it).
        if (n.text == "&&" || n.text == "||") {
            EmitExpression(*n.children[0]);
            Op jexpr = (n.text == "&&") ? Op::OP_JumpOnFalseExpr : Op::OP_JumpOnTrueExpr;
            EmitOp(jexpr);
            size_t at = here();
            EmitU16(0);
            EmitExpression(*n.children[1]);
            PatchForward16(at, n.line);
            return true;
        }
        EmitExpression(*n.children[0]);
        EmitExpression(*n.children[1]);
        Op op;
        if (!BinaryOpcode(n.text, op)) {
            Error(n.line, "unsupported binary operator '" + n.text + "'"); return true;
        }
        EmitOp(op);
        return true;
    }

    static bool BinaryOpcode(const std::string& t, Op& out) {
        if (t == "+") out = Op::OP_plus;
        else if (t == "-") out = Op::OP_minus;
        else if (t == "*") out = Op::OP_multiply;
        else if (t == "/") out = Op::OP_divide;
        else if (t == "%") out = Op::OP_mod;
        else if (t == "==") out = Op::OP_equality;
        else if (t == "!=") out = Op::OP_inequality;
        else if (t == "<") out = Op::OP_less;
        else if (t == ">") out = Op::OP_greater;
        else if (t == "<=") out = Op::OP_less_equal;
        else if (t == ">=") out = Op::OP_greater_equal;
        else if (t == "&") out = Op::OP_bit_and;
        else if (t == "|") out = Op::OP_bit_or;
        else if (t == "^") out = Op::OP_bit_ex_or;
        else if (t == "<<") out = Op::OP_shift_left;
        else if (t == ">>") out = Op::OP_shift_right;
        else return false;
        return true;
    }

    bool EmitUnary(const KisakAstNode& n) {
        if (n.children.empty() || !n.children[0]) {
            Error(n.line, "malformed unary expression"); return true;
        }
        const KisakAstNode& operand = *n.children[0];
        if (n.text == "++" || n.text == "--") {
            // Postfix increment/decrement: operate on the local's ref slot; no
            // value produced (see EmitExpression's return-value contract).
            if (operand.kind != Kind::IdentifierExpr) {
                Error(n.line, "++/-- requires a plain variable operand"); return false;
            }
            if (IsEntityKeyword(operand.text) || !locals.Has(operand.text)) {
                Error(n.line, "++/-- on undefined variable '" + operand.text + "'"); return false;
            }
            EmitRefLocal(locals.Cached(operand.text));
            EmitOp(n.text == "++" ? Op::OP_inc : Op::OP_dec);
            return false;
        }
        if (n.text == "!") {
            EmitExpression(operand); EmitOp(Op::OP_BoolNot); return true;
        }
        if (n.text == "~") {
            EmitExpression(operand); EmitOp(Op::OP_BoolComplement); return true;
        }
        if (n.text == "-") {
            // No unary-negate opcode; compute 0 - operand (int 0 promotes to
            // float against a float operand via the VM's Scr_CastWeakerPair).
            EmitOp(Op::OP_GetZero);
            EmitExpression(operand);
            EmitOp(Op::OP_minus);
            return true;
        }
        Error(n.line, "unsupported unary operator '" + n.text + "'");
        return true;
    }

    bool EmitCall(const KisakAstNode& n) {
        int builtin = KisakScriptFindBuiltinIndex(n.text);
        uint32_t argc = static_cast<uint32_t>(n.children.size());
        if (builtin >= 0) {
            // Builtins read args via Scr_GetInt(0)=top[-0], i.e. index 0 is the
            // LAST-pushed value (see kisak_script_vm_android.h). So push args
            // RIGHT-TO-LEFT: the first syntactic arg ends up on top.
            for (uint32_t i = argc; i-- > 0;) {
                if (n.children[i]) EmitExpression(*n.children[i]);
                else { EmitOp(Op::OP_GetUndefined); }
            }
            if (argc <= 5) {
                EmitOp(static_cast<Op>(static_cast<uint8_t>(Op::OP_CallBuiltin0) + argc));
            } else {
                EmitOp(Op::OP_CallBuiltin);
                EmitByte(static_cast<uint8_t>(argc));
            }
            EmitU16(static_cast<uint16_t>(builtin));
            return true;
        }
        if (functionNames.count(n.text)) {
            // Same-file script call. Caller pushes a PreCodePos delimiter, then
            // args LEFT-TO-RIGHT (the callee prologue's reverse-order
            // SafeCreateVariableFieldCached chain binds the LAST-pushed arg to
            // the LAST parameter). OP_ScriptFunctionCall's 4-byte codepos is a
            // byte offset into the same buffer, backpatched once the callee's
            // entry is known.
            EmitOp(Op::OP_PreScriptCall);
            for (uint32_t i = 0; i < argc; ++i) {
                if (n.children[i]) EmitExpression(*n.children[i]);
                else { EmitOp(Op::OP_GetUndefined); }
            }
            EmitOp(Op::OP_ScriptFunctionCall);
            size_t at = here();
            EmitU32(0);
            callFixups.push_back({at, n.text, curFunction ? *curFunction : "", n.line});
            return true;
        }
        if (locals.Has(n.text)) {
            // Step 3's third tier: call through a local variable that may
            // hold a FunctionRef at runtime (see decision #2 above for why
            // bare `name()` is allowed to mean this — a deliberate
            // simplification, not real GSC's `[[ expr ]]` syntax). Args
            // LEFT-TO-RIGHT same as a direct script call, then the
            // function-pointer expression is evaluated LAST so its value
            // is on TOP of stack when OP_ScriptFunctionCallPointer pops it.
            EmitOp(Op::OP_PreScriptCall);
            for (uint32_t i = 0; i < argc; ++i) {
                if (n.children[i]) EmitExpression(*n.children[i]);
                else { EmitOp(Op::OP_GetUndefined); }
            }
            EmitEvalLocal(locals.Cached(n.text));
            EmitOp(Op::OP_ScriptFunctionCallPointer);
            return true;
        }
        Error(n.line, "unknown function/builtin: '" + n.text +
                      "' (no builtin by that name, no function so-named in this program, "
                      "and no local variable by that name to call through; a bareword call "
                      "never resolves cross-file — use the path\\file::func(...) syntax for that)");
        return true;
    }

    // Step 2's bare-`::func`/`::func(...)` forms (empty stringList, no
    // filename) are same-file by construction — resolve exactly like an
    // ordinary call. EmitCall only reads n.text/n.children, not n.kind, so
    // it works unchanged for a NamespacedCallExpr node too. A NON-empty path
    // is cross-file: inside a cross-file session, emit a direct script call to
    // the qualified target (a CrossFileFixup links it — see below); outside
    // one, it is a compile error (nothing else to search).
    bool EmitNamespacedCall(const KisakAstNode& n) {
        if (n.stringList.empty()) return EmitCall(n);
        if (!crossFixups) {
            Error(n.line, "namespaced call '" + JoinBackslash(n.stringList) + "::" + n.text +
                          "(...)' needs a cross-file compile session "
                          "(CompileGscZoneEntryPoint); single-file compilation cannot "
                          "resolve a reference into another file");
            return true;
        }
        // A far call compiles exactly like a same-file script call (retail
        // emits the identical OP_ScriptFunctionCall) — only the fixup differs:
        // caller pushes a PreCodePos delimiter, args LEFT-TO-RIGHT, then the
        // 4-byte codepos operand is backpatched to the qualified target.
        EmitOp(Op::OP_PreScriptCall);
        uint32_t argc = static_cast<uint32_t>(n.children.size());
        for (uint32_t i = 0; i < argc; ++i) {
            if (n.children[i]) EmitExpression(*n.children[i]);
            else { EmitOp(Op::OP_GetUndefined); }
        }
        EmitOp(Op::OP_ScriptFunctionCall);
        RecordCrossFileFixup(n);
        return true;
    }

    // Step 2's bare `::func` value-reference form (FunctionRefExpr). Empty
    // path = same-file: resolve via the SAME same-file/forward-reference
    // backpatch mechanism a same-file CallExpr already gets (CallFixup is
    // generic over "a 4-byte codepos operand pointing at a same-Program
    // function" — it doesn't care whether the opcode before it was
    // OP_ScriptFunctionCall or OP_GetFunction). NON-empty path = cross-file
    // value reference: emit OP_GetFunction + a CrossFileFixup, resolvable only
    // inside a cross-file session.
    bool EmitFunctionRef(const KisakAstNode& n) {
        if (!n.stringList.empty()) {
            if (!crossFixups) {
                Error(n.line, "function reference '" + JoinBackslash(n.stringList) + "::" +
                              n.text + "' needs a cross-file compile session "
                              "(CompileGscZoneEntryPoint); single-file compilation cannot "
                              "resolve a reference into another file");
                return true;
            }
            EmitOp(Op::OP_GetFunction);
            RecordCrossFileFixup(n);
            return true;
        }
        if (!functionNames.count(n.text)) {
            Error(n.line, "unknown function: '::" + n.text +
                          "' (no function so-named in this file)");
            return true;
        }
        EmitOp(Op::OP_GetFunction);
        size_t at = here();
        EmitU32(0);
        callFixups.push_back({at, n.text, curFunction ? *curFunction : "", n.line});
        return true;
    }

    // Emit the 4-byte placeholder for a cross-file target and register it in the
    // shared fixup list + this file's precache list. Precondition: crossFixups
    // and precache are set (cross-file session), and n.stringList is non-empty.
    void RecordCrossFileFixup(const KisakAstNode& n) {
        std::string canonical = CanonicalGscName(n.stringList);
        std::string qualified = canonical + "::" + n.text;
        size_t at = here();
        EmitU32(0);
        crossFixups->push_back({at, qualified, curCanonical ? *curCanonical : "",
                                curFunction ? *curFunction : "", n.line});
        precache->push_back(canonical);
    }

    // Path segments as written in .gsc source use backslash; kept only for
    // human-facing error messages (`maps\_blackhawk::main`).
    static std::string JoinBackslash(const std::vector<std::string>& segments) {
        std::string out;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i) out += "\\";
            out += segments[i];
        }
        return out;
    }

    // Canonical rawfile name: segments joined with "/" plus ".gsc" — verified
    // against real killhouse.ff rawfile names (source `maps\killhouse_fx::main`
    // -> rawfile "maps/killhouse_fx.gsc"). Filenames are NOT unique across a
    // zone (killhouse_fx appears at both maps/ and maps/createfx/), which is
    // why the full path is preserved rather than just the leaf.
    static std::string CanonicalGscName(const std::vector<std::string>& segments) {
        std::string out;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i) out += "/";
            out += segments[i];
        }
        out += ".gsc";
        return out;
    }

    // ---- statement emission ----
    void EmitStatement(const KisakAstNode& n) {
        switch (n.kind) {
            case Kind::Block:
                for (const auto& c : n.children) if (c) EmitStatement(*c);
                break;
            case Kind::Assignment: EmitAssignment(n); break;
            case Kind::ExpressionStatement:
                if (!n.children.empty() && n.children[0]) EmitExprClause(*n.children[0]);
                break;
            case Kind::IfStatement: EmitIf(n); break;
            case Kind::WhileStatement: EmitWhile(n); break;
            case Kind::ForStatement: EmitFor(n); break;
            case Kind::ReturnStatement: EmitReturn(n); break;
            default:
                Error(n.line, "cannot compile statement node " + DescribeAstNodeKind(n.kind));
                break;
        }
    }

    // A bare expression used for its side effect (statement or for-clause). If
    // it leaves a value, discard it with OP_DecTop; ++/-- leave nothing.
    void EmitExprClause(const KisakAstNode& n) {
        if (n.kind == Kind::Assignment) { EmitAssignment(n); return; }
        bool produced = EmitExpression(n);
        if (produced) EmitOp(Op::OP_DecTop);
    }

    void EmitAssignment(const KisakAstNode& n) {
        if (n.children.size() != 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed assignment"); return;
        }
        const KisakAstNode& target = *n.children[0];
        const KisakAstNode& value = *n.children[1];
        if (target.kind == Kind::FieldAccessExpr) {
            Error(n.line, "field assignment '." + target.text + "' needs an entity/object model: " +
                          std::string(kEntityDeferred));
            return;
        }
        // Arrays blueprint (plans/android-gscript-arrays.md) step 3: an
        // ArrayIndexExpr target (`arr[key] = value`) writes into an array
        // element. Emitted as: base, key, OP_EvalArrayRef (establishes the
        // element ref), value, OP_SetVariableField (writes through it).
        if (target.kind == Kind::ArrayIndexExpr) {
            if (target.children.size() != 2 || !target.children[0] || !target.children[1]) {
                Error(n.line, "malformed array subscript assignment"); return;
            }
            if (!n.text.empty()) {
                // Compound assignment on an array element (`arr[key] += v`) is a
                // deliberate, documented scope cut. No real corpus GSC uses it
                // (grep of the device-extracted .gsc for `[...] OP=` found none),
                // and doing it correctly requires establishing the element ref
                // ONCE and read-modify-writing through it, to avoid double-
                // evaluating the key expression's side effects (e.g. the `i++`
                // in `arr[i++] += v`). This port has no array-element ref-READ
                // opcode wired, and there is no established idiom here to reuse
                // safely, so reject explicitly rather than silently miscompile.
                Error(n.line, "compound assignment '" + n.text + "' on an array element is "
                              "not supported (arrays blueprint step 3 scope cut — no real "
                              "corpus uses it); use a plain '=' assignment instead");
                return;
            }
            // Emit the base through the SAME EmitExpression path every other
            // expression takes — so a FieldAccessExpr base (`level.x[0] = v`)
            // still correctly hits EmitExpression's entity-deferred rejection
            // rather than slipping past it via a shortcut.
            EmitExpression(*target.children[0]);   // base -> array on stack
            EmitExpression(*target.children[1]);   // key on top
            EmitOp(Op::OP_EvalArrayRef);
            EmitExpression(value);
            EmitOp(Op::OP_SetVariableField);
            return;
        }
        if (target.kind != Kind::IdentifierExpr) {
            Error(n.line, "assignment target must be a plain variable"); return;
        }
        if (IsEntityKeyword(target.text)) {
            Error(n.line, "assignment to '" + target.text + "': " + std::string(kEntityDeferred));
            return;
        }
        if (!locals.Has(target.text)) {
            // Pre-scan should have registered every assigned identifier; a miss
            // means an internal inconsistency, not user error.
            Error(n.line, "internal: local '" + target.text + "' was not pre-allocated");
            return;
        }
        int cached = locals.Cached(target.text);
        if (n.text.empty()) {
            // Plain `v = expr`.
            EmitExpression(value);
        } else {
            // Compound `v OP= expr` -> v = v OP expr. Read v, emit expr, apply.
            EmitEvalLocal(cached);
            EmitExpression(value);
            Op op;
            std::string base = n.text.substr(0, 1);  // "+=" -> "+"
            if (!BinaryOpcode(base, op)) {
                Error(n.line, "unsupported compound assignment '" + n.text + "'"); return;
            }
            EmitOp(op);
        }
        EmitSetLocal(cached);
    }

    void EmitIf(const KisakAstNode& n) {
        if (n.children.size() < 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed if statement"); return;
        }
        EmitExpression(*n.children[0]);
        size_t jfalse = EmitJumpOnFalsePlaceholder();
        EmitStatement(*n.children[1]);
        bool hasElse = n.children.size() >= 3 && n.children[2];
        if (hasElse) {
            size_t jend = EmitJumpPlaceholder();
            PatchForward16(jfalse, n.line);   // false -> else branch
            EmitStatement(*n.children[2]);
            PatchForward32(jend);             // then-branch -> past else
        } else {
            PatchForward16(jfalse, n.line);
        }
    }

    void EmitWhile(const KisakAstNode& n) {
        if (n.children.size() < 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed while statement"); return;
        }
        size_t top = here();
        EmitExpression(*n.children[0]);
        size_t jfalse = EmitJumpOnFalsePlaceholder();
        EmitStatement(*n.children[1]);
        EmitJumpBackTo(top, n.line);
        PatchForward16(jfalse, n.line);
    }

    void EmitFor(const KisakAstNode& n) {
        // children: [0]=init (optional), [1]=cond (optional), [2]=incr (optional),
        // [3]=body. init/incr are bare expr/assignment nodes (not statements).
        if (n.children.size() < 4) { Error(n.line, "malformed for statement"); return; }
        if (n.children[0]) EmitExprClause(*n.children[0]);
        size_t condTop = here();
        size_t jfalse = 0;
        bool hasCond = n.children[1] != nullptr;
        if (hasCond) {
            EmitExpression(*n.children[1]);
            jfalse = EmitJumpOnFalsePlaceholder();
        }
        if (n.children[3]) EmitStatement(*n.children[3]);
        if (n.children[2]) EmitExprClause(*n.children[2]);
        EmitJumpBackTo(condTop, n.line);
        if (hasCond) PatchForward16(jfalse, n.line);
    }

    void EmitReturn(const KisakAstNode& n) {
        if (!n.children.empty() && n.children[0]) EmitExpression(*n.children[0]);
        else EmitOp(Op::OP_GetUndefined);  // OP_Return always pops a value
        EmitOp(Op::OP_Return);
    }

    // ---- function + program emission ----
    void EmitFunction(const KisakAstNode& fn) {
        curFunction = &fn.text;
        uint32_t entry = static_cast<uint32_t>(here());
        program.functionEntryPoints[fn.text] = entry;
        // In a cross-file session, ALSO register the qualified key so other
        // files (and the entry-point lookup) can find this function past the
        // bare-name clobbering that shared compilation causes.
        if (curCanonical) {
            program.qualifiedFunctionEntryPoints[*curCanonical + "::" + fn.text] = entry;
        }

        // Fixed local frame (decision #1): parameters, then discovered body
        // locals. Params are created newest-last so parameter i lands at
        // creation order (nparams-1-i) — see the prologue emission below.
        locals = FunctionLocals{};
        int nparams = static_cast<int>(fn.stringList.size());
        for (int i = 0; i < nparams; ++i) {
            // creation order of param i = nparams-1-i (params SafeCreate'd in
            // reverse), recorded so Cached() matches how the VM binds them.
            locals.creationOrder[fn.stringList[i]] = nparams - 1 - i;
        }
        locals.total = nparams;
        std::vector<std::string> extraOrder;
        const KisakAstNode* body = (!fn.children.empty() && fn.children[0]) ? fn.children[0].get() : nullptr;
        if (body) CollectLocals(*body, locals, extraOrder);
        locals.total = nparams + static_cast<int>(extraOrder.size());

        // Prologue. SafeCreateVariableFieldCached in REVERSE parameter order
        // binds each pushed argument (the caller pushed them left-to-right on
        // top of a PreCodePos marker); checkclearparams then converts that
        // marker into the frame's CodePos boundary. CreateLocalVariable for the
        // remaining body locals (they don't touch the value stack).
        for (int i = nparams - 1; i >= 0; --i) {
            (void)i;
            EmitOp(Op::OP_SafeCreateVariableFieldCached);
            EmitU16(0);  // name id unused by this trimmed VM
        }
        EmitOp(Op::OP_checkclearparams);
        for (size_t i = 0; i < extraOrder.size(); ++i) {
            EmitOp(Op::OP_CreateLocalVariable);
            EmitU16(0);
        }

        if (body) EmitStatement(*body);

        // Fall-through terminator. A body ending in `return;` gets an
        // unreachable OP_End after its OP_Return — harmless, and cheaper than
        // tracking terminal-ness through every nested branch.
        EmitOp(Op::OP_End);
        curFunction = nullptr;
    }

    void Compile(const KisakAstNode& prog) {
        if (prog.kind != Kind::Program) {
            errors.push_back("compile: root AST node is not a Program");
            return;
        }
        // Pre-scan: every function name, with duplicate detection, so calls can
        // resolve forward references during emission.
        for (const auto& child : prog.children) {
            if (!child || child->kind != Kind::FunctionDef) continue;
            if (!functionNames.insert(child->text).second) {
                errors.push_back("compile: duplicate function definition '" + child->text + "'");
            }
        }
        for (const auto& child : prog.children) {
            if (child && child->kind == Kind::FunctionDef) EmitFunction(*child);
        }
        // Backpatch script-call codepos operands now that every entry offset
        // is known.
        for (const CallFixup& fx : callFixups) {
            auto it = program.functionEntryPoints.find(fx.callee);
            if (it == program.functionEntryPoints.end()) {
                errors.push_back("function " + fx.inFunction + ", line " +
                                 std::to_string(fx.line) + ": call to undefined function '" +
                                 fx.callee + "'");
                continue;
            }
            PatchU32(fx.at, it->second);
        }
    }
};

}  // namespace

KisakScriptCompileResult CompileGscAst(const KisakAstNode& program) {
    KisakScriptCompileResult result;
    Compiler c(result.program);  // emit directly into the result's program
    c.Compile(program);
    result.errors = std::move(c.errors);
    return result;
}

KisakScriptCompileResult CompileGscSource(const std::string& source) {
    KisakScriptParseResult parsed = ParseGscSource(source);
    if (parsed.program == nullptr || !parsed.errors.empty()) {
        KisakScriptCompileResult result;
        for (const auto& e : parsed.errors) result.errors.push_back("parse: " + e);
        if (result.errors.empty()) result.errors.push_back("parse: failed with no message");
        return result;
    }
    return CompileGscAst(*parsed.program);
}

KisakScriptCrossFileCompileResult CompileGscZoneEntryPoint(
    const std::string& entryCanonicalName, const KisakScriptFileLoader& loadFile) {
    KisakScriptCrossFileCompileResult result;
    result.entryCanonical = entryCanonicalName;

    std::vector<CrossFileFixup> crossFixups;  // shared across every file's Compiler
    std::unordered_set<std::string> compiled;  // canonical names already emitted
    std::unordered_set<std::string> scheduled{entryCanonicalName};
    std::vector<std::string> worklist{entryCanonicalName};

    // FIFO over the transitive closure of referenced files: the entry file
    // first, then each newly-referenced file in discovery order. Same-file
    // forward references are resolved inside each file's own Compile() pass
    // (CallFixup); cross-file forward references are deferred to the backpatch
    // pass below, after every file has registered its qualified functions.
    while (!worklist.empty()) {
        std::string canonical = worklist.front();
        worklist.erase(worklist.begin());
        if (!compiled.insert(canonical).second) continue;

        std::optional<std::string> source = loadFile(canonical);
        if (!source) {
            // Matches retail's CompileError("Could not find script '%s'"). This
            // is EXACTLY what a reference like maps\_blackhawk::main() produces,
            // since _blackhawk.gsc is in no zone this port scans — expected, not
            // a bug in this driver.
            result.errors.push_back("could not find script '" + canonical + "'");
            continue;
        }
        KisakScriptParseResult parsed = ParseGscSource(*source);
        if (parsed.program == nullptr || !parsed.errors.empty()) {
            for (const auto& e : parsed.errors)
                result.errors.push_back(canonical + " parse: " + e);
            if (parsed.errors.empty())
                result.errors.push_back(canonical + " parse: failed with no message");
            continue;
        }

        std::vector<std::string> filePrecache;
        Compiler c(result.program);
        c.curCanonical = &canonical;
        c.crossFixups = &crossFixups;
        c.precache = &filePrecache;
        c.Compile(*parsed.program);
        for (const auto& e : c.errors) result.errors.push_back(canonical + ": " + e);
        result.compiledFiles.push_back(canonical);

        for (const std::string& ref : filePrecache) {
            if (scheduled.insert(ref).second) worklist.push_back(ref);
        }
    }

    // Cross-file backpatch: every deferred (file,func) call site, resolved now
    // that all files have registered their qualified entry offsets.
    for (const CrossFileFixup& fx : crossFixups) {
        auto it = result.program.qualifiedFunctionEntryPoints.find(fx.qualified);
        if (it == result.program.qualifiedFunctionEntryPoints.end()) {
            result.errors.push_back(fx.inFile + ": function " + fx.inFunction + ", line " +
                                    std::to_string(fx.line) +
                                    ": cross-file call to undefined function '" +
                                    fx.qualified + "'");
            continue;
        }
        std::memcpy(&result.program.bytecode[fx.at], &it->second, 4);
        ++result.crossFileCallsResolved;
    }
    return result;
}
