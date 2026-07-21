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

// Switch/loop-control blueprint (plans/android-gscript-switch-control-
// flow.md) step 2: a compile-time-only "enclosing construct" context,
// pushed/popped around EmitWhile/EmitFor's own bodies (and, from step 3
// onward, EmitSwitch's own case-body block) -- NOT a runtime/VM concept,
// purely compiler bookkeeping for break;/continue;. `break;` works
// identically for all three kinds: a forward OP_jump, fixed up to "here"
// once the whole construct's own code is emitted. `continue;`'s target
// differs by kind -- corrected by adversarial review (H2) before any code
// was written: a `while` loop's condition re-check position is KNOWN in
// advance (an immediate backward jump), but a `for` loop's increment
// clause is emitted AFTER the body, so `continue;` there is a forward
// reference to a not-yet-emitted position and needs its own fixup list,
// patched once EmitFor reaches the body/increment boundary. A `Switch`
// context has no continue mechanism at all -- `continue;` inside a switch
// must search PAST it to find an enclosing loop.
enum class LoopOrSwitchKind : uint8_t { WhileLoop, ForLoop, Switch };
struct LoopOrSwitchContext {
    LoopOrSwitchKind kind;
    std::vector<size_t> breakFixups;     // all kinds: forward OP_jump operands -> "here" at construct end
    std::vector<size_t> continueFixups;  // ForLoop only: forward OP_jump operands -> the body/increment boundary
    size_t whileCondTop = 0;             // WhileLoop only: known backward-jump target for continue
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

    // Switch/loop-control blueprint step 2: the enclosing-construct stack
    // break;/continue; resolve against. Per-function in spirit (cleared
    // implicitly since it's always empty again at every function boundary
    // in well-formed code -- push/pop is always balanced within one
    // function body), but kept at Compiler scope like `locals` since this
    // Compiler processes one function body at a time either way.
    std::vector<LoopOrSwitchContext> loopSwitchStack;

    // Switch/loop-control blueprint step 3: SwitchStatement node -> its
    // hidden local slot's generated name (see CollectLocals's own comment).
    // Populated once per function during the locals pre-scan, read by
    // EmitSwitch — a single source of truth, not two independently-
    // recomputed counters.
    std::unordered_map<const KisakAstNode*, std::string> switchSlotNames;

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
    // Entity/object-model blueprint step 4: needed by the field-array
    // auto-vivification codegen (EmitFieldArrayAutoVivify) -- mirrors
    // EmitJumpOnFalsePlaceholder above exactly, just OP_JumpOnTrue.
    size_t EmitJumpOnTruePlaceholder() {
        EmitOp(Op::OP_JumpOnTrue);
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

    // ---- entity/object-model field eval / assign (step 4) ----
    // Field read: emit the base object expression, then OP_EvalFieldVariable
    // with the field name as a stringPool operand (Architecture fact 4 --
    // encoded like OP_GetString's operand, not like OP_EvalArrayRef's
    // runtime-popped key, since a field name is a compile-time constant).
    void EmitFieldReadRaw(const KisakAstNode& fieldBase, uint16_t fieldId) {
        EmitExpression(fieldBase);
        EmitOp(Op::OP_EvalFieldVariable);
        EmitU16(fieldId);
    }
    // Field write-ref: same operand convention, establishes a RefKind::
    // ObjectField ref for the following OP_SetVariableField to consume.
    // Pushes nothing -- unlike EmitFieldReadRaw, this pops the base object
    // and leaves the stack as it was before.
    void EmitFieldRef(const KisakAstNode& fieldBase, uint16_t fieldId) {
        EmitExpression(fieldBase);
        EmitOp(Op::OP_EvalFieldVariableRef);
        EmitU16(fieldId);
    }

    // ---- pre-scan: discover every local a function assigns to ----
    // Walks the body in source (pre-)order; any Assignment whose target is a
    // plain IdentifierExpr (not an entity keyword, not already a param/known
    // local) declares a new local. Field-access targets are left alone here —
    // they're rejected at emit time with the entity-model message.
    //
    // Switch/loop-control blueprint step 3: also reserves one HIDDEN local
    // slot per SwitchStatement node encountered, so the switch's subject
    // expression can be evaluated exactly once and re-read by every
    // comparison in the dispatch prologue (Architecture fact 3). Keyed by
    // the node's own address in `switchSlotNames` (populated HERE, read
    // later by EmitSwitch) rather than a separately-recomputed counter in
    // each of the two traversals — corrected by adversarial review (M4):
    // two independent traversals re-deriving "the same" counter is a
    // fragile coupling; a single map populated once and read once sidesteps
    // it entirely. The generated name (`$switch0`, `$switch1`, ...) uses a
    // leading `$`, a character this port's lexer's IsIdentStart/IsIdentCont
    // cannot produce as part of a real identifier (confirmed against
    // kisak_script_lexer_android.cpp) -- a leading `__` would NOT be
    // collision-proof, since `_` is an ordinary identifier-start character
    // here and a real script could name a variable `__switch_tmp`.
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
        if (node.kind == Kind::SwitchStatement) {
            std::string hiddenName = "$switch" + std::to_string(switchSlotNames.size());
            switchSlotNames[&node] = hiddenName;
            if (!out.Has(hiddenName)) {
                out.creationOrder[hiddenName] = static_cast<int>(order.size()) + out.total;
                order.push_back(hiddenName);
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
            // Entity/object-model blueprint (plans/android-gscript-entity-model.md)
            // step 4: object-prefixed call, non-threaded (e.g. bog_a_extract.gsc:
            // 807's `self set_force_color("c");`). Always bareword -- the
            // parser's MethodCallExpr lookahead (parser.cpp:337-348) can never
            // produce a namespaced path -- so pathSegments is always empty here.
            case Kind::MethodCallExpr:
                if (n.children.empty() || !n.children[0]) {
                    Error(n.line, "malformed method call"); return true;
                }
                return EmitMethodCallLike(*n.children[0], n.text, {}, n, /*argsStart=*/1,
                                          n.line, /*threaded=*/false);
            // Entity/object-model blueprint step 4: generic field read
            // (Architecture fact 4) -- reuses the existing OP_EvalFieldVariable
            // opcode (Step 1).
            case Kind::FieldAccessExpr:
                if (n.children.empty() || !n.children[0]) {
                    Error(n.line, "malformed field access"); return true;
                }
                EmitFieldReadRaw(*n.children[0], InternString(n.text));
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
            // Entity/object-model blueprint step 4: bare self/level/game
            // reference -- reuses the existing OP_GetSelf/OP_GetLevel/
            // OP_GetGame opcodes (Step 1). `anim` stays explicitly rejected
            // (Scope Cut item 1: unused by the real corpus at every current
            // failure boundary across all five blueprints' worth of
            // validation this session) -- the VM has no OP_GetAnim handler
            // at all, so emitting it here would only fail later, at
            // runtime, with a confusing "unsupported opcode"; rejecting it
            // here at compile time, with a clear reason, is more honest.
            if (n.text == "self") { EmitOp(Op::OP_GetSelf); return true; }
            if (n.text == "level") { EmitOp(Op::OP_GetLevel); return true; }
            if (n.text == "game") { EmitOp(Op::OP_GetGame); return true; }
            Error(n.line, "reference to 'anim' is out of scope (plans/android-gscript-"
                          "entity-model.md, Scope Cut item 1 -- unused by the real corpus "
                          "at every current failure boundary; self/level/game are supported)");
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

    // Threading blueprint (plans/android-gscript-threading.md) step 3:
    // `threaded` parameterizes which opcode the same-file/cross-file script-
    // call tier emits (OP_ScriptThreadCall vs. OP_ScriptFunctionCall) — the
    // ONE thing that differs between a plain call and `thread <call>;`
    // targeting the SAME callee resolution. Defaults to false so every
    // existing call site (ordinary CallExpr/NamespacedCallExpr emission)
    // is unaffected. The builtin tier never changes behavior for `threaded`
    // — real GSC never threads a builtin (confirmed: no real corpus usage
    // does; builtins are always synchronous engine calls in retail too, so
    // there is no distinct "threaded builtin" semantics to model even if it
    // occurred) — it always compiles the same way regardless. The local-
    // FunctionRef-pointer tier has no threaded equivalent at all
    // (OP_ScriptThreadCallPointer is deliberately unimplemented per step 1)
    // and is rejected with a specific error when threaded=true, rather than
    // silently emitting the wrong (non-threaded) opcode.
    bool EmitCall(const KisakAstNode& n, bool threaded = false) {
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
            EmitOp(threaded ? Op::OP_ScriptThreadCall : Op::OP_ScriptFunctionCall);
            size_t at = here();
            EmitU32(0);
            callFixups.push_back({at, n.text, curFunction ? *curFunction : "", n.line});
            return true;
        }
        if (locals.Has(n.text)) {
            if (threaded) {
                // Threading blueprint step 3: no OP_ScriptThreadCallPointer
                // exists (deliberately unimplemented, step 1) -- reject
                // rather than silently emit the non-threaded pointer call.
                Error(n.line, "'thread " + n.text + "(...)' through a local function-pointer "
                              "variable is not supported (no threaded-call-through-pointer "
                              "opcode exists in this port) — call it directly instead of "
                              "threading it, or thread the same-named function itself");
                return true;
            }
            // Step 3's (namespaced-calls blueprint) third tier: call through
            // a local variable that may hold a FunctionRef at runtime (see
            // decision #2 above for why bare `name()` is allowed to mean
            // this — a deliberate simplification, not real GSC's
            // `[[ expr ]]` syntax). Args LEFT-TO-RIGHT same as a direct
            // script call, then the function-pointer expression is
            // evaluated LAST so its value is on TOP of stack when
            // OP_ScriptFunctionCallPointer pops it.
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
    // `threaded`: see EmitCall's own comment — same parameterization,
    // threaded through to EmitCall for the empty-path (same-file) case.
    bool EmitNamespacedCall(const KisakAstNode& n, bool threaded = false) {
        if (n.stringList.empty()) return EmitCall(n, threaded);
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
        // `threaded` swaps in OP_ScriptThreadCall the same way EmitCall's
        // same-file tier does — a cross-file namespaced target can be
        // threaded exactly like a same-file one (killhouse.gsc's own
        // `thread maps\_introscreen::introscreen_feed_lines(lines);`).
        EmitOp(Op::OP_PreScriptCall);
        uint32_t argc = static_cast<uint32_t>(n.children.size());
        for (uint32_t i = 0; i < argc; ++i) {
            if (n.children[i]) EmitExpression(*n.children[i]);
            else { EmitOp(Op::OP_GetUndefined); }
        }
        EmitOp(threaded ? Op::OP_ScriptThreadCall : Op::OP_ScriptFunctionCall);
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

    // Entity/object-model blueprint step 4: shared codegen for both
    // object-prefixed call forms -- MethodCallExpr (non-threaded, always
    // bareword: the parser's lookahead at parser.cpp:337-348 only ever
    // fires on a plain Identifier, never a namespaced path) and Step 3's
    // MethodThreadCallStatement (threaded, bareword OR namespaced -- wraps
    // an ordinary CallExpr/NamespacedCallExpr from the pre-existing call
    // grammar, e.g. killhouse.gsc:221's `level thread maps\killhouse_amb::
    // main();`). Unlike EmitCall's bareword resolution, real GSC's
    // method-call syntax NEVER targets a builtin (builtins take no implicit
    // receiver) and this port's local-FunctionRef-pointer tier has no
    // receiver-rebinding opcode either (OP_ScriptMethodCallPointer/
    // OP_ScriptMethodThreadCallPointer are deliberately unimplemented, Step
    // 2) -- only the same-file/cross-file SCRIPT-FUNCTION tiers apply here.
    //
    // Calling convention: push PreCodePos + args LEFT-TO-RIGHT (identical to
    // EmitCall), then push the RECEIVER expression LAST, so it is on TOP of
    // the stack for OP_ScriptMethodCall/OP_ScriptMethodThreadCall's pop
    // (Architecture facts 2/6) -- the callee's own prologue then sees
    // exactly the same [PreCodePos, arg1..argN] shape an ordinary call
    // would leave.
    //
    // `argsHost`/`argsStart`: the call's arguments live as a slice of some
    // node's children (MethodCallExpr: children[1..]; the wrapped call
    // node's own children[0..] for the threaded form) -- passed this way
    // rather than materialized as a copy, since KisakAstNode::children is a
    // vector<unique_ptr>, not copyable.
    bool EmitMethodCallLike(const KisakAstNode& receiver, const std::string& funcName,
                            const std::vector<std::string>& pathSegments,
                            const KisakAstNode& argsHost, size_t argsStart,
                            uint32_t line, bool threaded) {
        Op op = threaded ? Op::OP_ScriptMethodThreadCall : Op::OP_ScriptMethodCall;
        if (pathSegments.empty()) {
            if (!functionNames.count(funcName)) {
                if (locals.Has(funcName)) {
                    // Real GSC has no receiver-rebinding call-through-pointer
                    // syntax, and this port's OP_ScriptMethodCallPointer/
                    // OP_ScriptMethodThreadCallPointer are deliberately
                    // unimplemented (Step 2) -- reject with a specific
                    // message rather than a generic "unknown function"
                    // (mirrors EmitCall's own threaded-local-pointer
                    // rejection above).
                    Error(line, "'" + funcName + "(...)' through a local function-pointer "
                                "variable, used as an object-prefixed call target, is not "
                                "supported (no method-call-through-pointer opcode exists in "
                                "this port) — call a same-file script function by name instead");
                    return true;
                }
                Error(line, "unknown function '" + funcName + "' for object-prefixed call "
                            "(method-call syntax only resolves same-file/cross-file script "
                            "functions in this port -- no builtin tier, matching real GSC's "
                            "own restriction that a receiver always binds to a script "
                            "function, never a builtin)");
                return true;
            }
        } else if (!crossFixups) {
            Error(line, "namespaced object-prefixed call '" + JoinBackslash(pathSegments) + "::" +
                        funcName + "(...)' needs a cross-file compile session "
                        "(CompileGscZoneEntryPoint); single-file compilation cannot resolve "
                        "a reference into another file");
            return true;
        }
        EmitOp(Op::OP_PreScriptCall);
        for (size_t i = argsStart; i < argsHost.children.size(); ++i) {
            if (argsHost.children[i]) EmitExpression(*argsHost.children[i]);
            else { EmitOp(Op::OP_GetUndefined); }
        }
        EmitExpression(receiver);   // receiver LAST -- on top for the opcode's pop
        EmitOp(op);
        size_t at = here();
        EmitU32(0);
        if (pathSegments.empty()) {
            callFixups.push_back({at, funcName, curFunction ? *curFunction : "", line});
        } else {
            std::string canonical = CanonicalGscName(pathSegments);
            std::string qualified = canonical + "::" + funcName;
            crossFixups->push_back({at, qualified, curCanonical ? *curCanonical : "",
                                    curFunction ? *curFunction : "", line});
            precache->push_back(canonical);
        }
        return true;
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
            // Threading blueprint (plans/android-gscript-threading.md) step
            // 3: `thread <call>;` -- emit the SAME argument-evaluation
            // sequence an ordinary call to the same target would use
            // (EmitCall/EmitNamespacedCall, threaded=true swaps in
            // OP_ScriptThreadCall), then discard the (per real GSC, always
            // unused) return value with a trailing OP_DecTop -- mirroring
            // EmitExprClause's existing discard pattern exactly. This is
            // step 1's already-resolved design: OP_ScriptThreadCall has no
            // hook to discard its own result (the callee hasn't run yet at
            // its own dispatch site), so the compiler must do it here.
            case Kind::ThreadCallStatement: {
                if (n.children.empty() || !n.children[0]) {
                    Error(n.line, "malformed thread statement"); break;
                }
                const KisakAstNode& call = *n.children[0];
                bool produced = (call.kind == Kind::NamespacedCallExpr)
                    ? EmitNamespacedCall(call, /*threaded=*/true)
                    : EmitCall(call, /*threaded=*/true);
                if (produced) EmitOp(Op::OP_DecTop);
                break;
            }
            // Switch/loop-control blueprint step 2: `break;`/`continue;`.
            // See EmitBreak/EmitContinue and the LoopOrSwitchContext
            // comment above for the full design.
            case Kind::BreakStatement: EmitBreak(n); break;
            case Kind::ContinueStatement: EmitContinue(n); break;
            case Kind::SwitchStatement: EmitSwitch(n); break;
            // `wait <expr>;` -- emit the duration, then OP_wait (validates
            // type/range, documented no-op otherwise — step 1).
            case Kind::WaitStatement: {
                if (n.children.empty() || !n.children[0]) {
                    Error(n.line, "malformed wait statement"); break;
                }
                EmitExpression(*n.children[0]);
                EmitOp(Op::OP_wait);
                break;
            }
            // Entity/object-model blueprint step 4: `<expr> thread <call>;`
            // (killhouse.gsc:221/cargoship_extract.gsc:172's shared
            // `level thread maps\<file>::main();` blocker). children[0] =
            // receiver, children[1] = the call being threaded (a plain
            // CallExpr or NamespacedCallExpr from the pre-existing call
            // grammar -- its own .stringList is empty for a bareword call,
            // non-empty for a namespaced one, so EmitMethodCallLike doesn't
            // need to branch on call.kind at all). Same trailing-OP_DecTop
            // discard mechanism as bare ThreadCallStatement above.
            case Kind::MethodThreadCallStatement: {
                if (n.children.size() != 2 || !n.children[0] || !n.children[1]) {
                    Error(n.line, "malformed object-prefixed thread statement"); break;
                }
                const KisakAstNode& receiver = *n.children[0];
                const KisakAstNode& call = *n.children[1];
                if (call.kind != Kind::CallExpr && call.kind != Kind::NamespacedCallExpr) {
                    Error(n.line, "malformed object-prefixed thread statement (call target "
                                  "must be a function call)");
                    break;
                }
                bool produced = EmitMethodCallLike(receiver, call.text, call.stringList,
                                                   call, /*argsStart=*/0, n.line, /*threaded=*/true);
                if (produced) EmitOp(Op::OP_DecTop);
                break;
            }
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

    // Entity/object-model blueprint step 4: auto-vivification for
    // `<fieldBase>.<fieldName>[key] = value` when the field has never been
    // written (real corpus: cargoship_extract.gsc:10, the FIRST statement of
    // main(): `level.fogvalue["near"] = 100;`, with no preceding
    // `level.fogvalue = [];`). CORRECTED BY ADVERSARIAL REVIEW: real GSC
    // auto-creates the array on first indexed write to an unset field, and
    // this port's existing array machinery does NOT do that "for free" --
    // an earlier draft of this plan wrongly assumed it did. Implemented
    // entirely with EXISTING opcodes (no new opcode IDs): read the field,
    // test it with the already-implemented isdefined() builtin, and if
    // undefined, establish a field ref and store a fresh empty array
    // through it. The caller (EmitAssignment's ArrayIndexExpr branch) then
    // does its OWN, separate, ordinary field read afterward (via the
    // unchanged EmitExpression(FieldAccessExpr) path) to obtain the
    // now-guaranteed-Array value -- this function only handles the
    // conditional creation, not the final read.
    //
    // `fieldBase` is evaluated TWICE here (once for the test read, once for
    // the vivify-write's ref) plus a THIRD time by the caller's own final
    // read afterward. This is only correct because every real use is
    // self/level/game (a keyword reference, idempotent and side-effect-free
    // to re-evaluate per Step 1's singleton design) -- documented here as
    // the scope this relies on, not assumed silently.
    void EmitFieldArrayAutoVivify(const KisakAstNode& fieldBase, const std::string& fieldName,
                                  uint32_t line) {
        int isdefinedIdx = KisakScriptFindBuiltinIndex("isdefined");
        if (isdefinedIdx < 0) {
            Error(line, "internal: 'isdefined' builtin missing (needed for field-array "
                        "auto-vivification)");
            return;
        }
        uint16_t fieldId = InternString(fieldName);
        EmitFieldReadRaw(fieldBase, fieldId);           // [fieldVal]
        EmitOp(Op::OP_CallBuiltin1);
        EmitU16(static_cast<uint16_t>(isdefinedIdx));    // pops fieldVal, pushes 0/1 [isDefinedFlag]
        size_t jTrueAt = EmitJumpOnTruePlaceholder();    // already defined -> skip vivify block
        EmitFieldRef(fieldBase, fieldId);                // establishes ref, pops a fresh obj eval
        EmitOp(Op::OP_EmptyArray);                        // [emptyArr]
        EmitOp(Op::OP_SetVariableField);                  // writes through the ref, pops emptyArr
        PatchForward16(jTrueAt, line);
    }

    void EmitAssignment(const KisakAstNode& n) {
        if (n.children.size() != 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed assignment"); return;
        }
        const KisakAstNode& target = *n.children[0];
        const KisakAstNode& value = *n.children[1];
        if (target.kind == Kind::FieldAccessExpr) {
            // Entity/object-model blueprint step 4: field write
            // (Architecture fact 5). Reuses the existing
            // OP_EvalFieldVariableRef + OP_SetVariableField dispatch
            // (Step 1) -- no new setter opcode.
            if (target.children.empty() || !target.children[0]) {
                Error(n.line, "malformed field access"); return;
            }
            uint16_t fieldId = InternString(target.text);
            EmitFieldRef(*target.children[0], fieldId);   // establishes ref, pops obj
            if (n.text.empty()) {
                EmitExpression(value);
            } else {
                // Compound `v OP= expr` on a field (real corpus:
                // cargoship_extract.gsc:2860, `self.baseaccuracy *= .8;`) --
                // re-read through a SEPARATE field read (the ref established
                // above persists on the frame across this, unaffected),
                // apply the operator, leave the result on top for
                // OP_SetVariableField below. Safe to re-evaluate the base a
                // second time here -- unlike the array-element compound-
                // assign scope cut below, there is no runtime-evaluated KEY
                // expression for a plain field access to double-evaluate;
                // the field name is a compile-time constant.
                EmitFieldReadRaw(*target.children[0], fieldId);   // [curVal]
                EmitExpression(value);                             // [curVal, rhs]
                Op op;
                std::string base = n.text.substr(0, 1);
                if (!BinaryOpcode(base, op)) {
                    Error(n.line, "unsupported compound assignment '" + n.text + "'"); return;
                }
                EmitOp(op);
            }
            EmitOp(Op::OP_SetVariableField);
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
            // Entity/object-model blueprint step 4: if the base is a field
            // access (`level.fogvalue[key] = v`), auto-vivify an empty array
            // into that field FIRST when it's currently unset (real corpus:
            // cargoship_extract.gsc:10, `level.fogvalue["near"] = 100;`,
            // the corpus's own headline auto-vivification case -- see
            // EmitFieldArrayAutoVivify's own comment for why this is
            // genuinely new work, not something the existing array
            // machinery already handled for free).
            if (target.children[0]->kind == Kind::FieldAccessExpr) {
                const KisakAstNode& fa = *target.children[0];
                if (fa.children.empty() || !fa.children[0]) {
                    Error(n.line, "malformed field access"); return;
                }
                EmitFieldArrayAutoVivify(*fa.children[0], fa.text, n.line);
            }
            // Emit the base through the SAME EmitExpression path every other
            // expression takes — so a FieldAccessExpr base (`level.x[0] = v`)
            // now correctly resolves to a real field read (Step 4's own
            // EmitExpression::FieldAccessExpr codegen above), guaranteed to
            // be an Array by the auto-vivification just above when it
            // wasn't already.
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

    // Switch/loop-control blueprint step 2: `break;` -- identical for
    // while/for/switch (a forward OP_jump, fixed up to "here" once the
    // TOP context's own construct finishes emitting). A stray break;
    // outside any context is a specific compile error, never a crash.
    void EmitBreak(const KisakAstNode& n) {
        if (loopSwitchStack.empty()) {
            Error(n.line, "'break' used outside any loop or switch");
            return;
        }
        size_t at = EmitJumpPlaceholder();
        loopSwitchStack.back().breakFixups.push_back(at);
    }

    // `continue;` -- searches from the TOP of the stack for the nearest
    // LOOP entry (a `Switch` context is skipped, per Architecture fact 2 --
    // a switch has no continue target of its own). `while`'s target is
    // already known (an immediate backward jump); `for`'s is a forward
    // reference resolved later by EmitFor itself (see its own comment).
    // continue; found outside any loop (even if inside a switch with no
    // enclosing loop) is a specific compile error.
    void EmitContinue(const KisakAstNode& n) {
        for (auto it = loopSwitchStack.rbegin(); it != loopSwitchStack.rend(); ++it) {
            if (it->kind == LoopOrSwitchKind::WhileLoop) {
                EmitJumpBackTo(it->whileCondTop, n.line);
                return;
            }
            if (it->kind == LoopOrSwitchKind::ForLoop) {
                size_t at = EmitJumpPlaceholder();
                it->continueFixups.push_back(at);
                return;
            }
            // Switch: continue is not meaningful here, keep searching outward.
        }
        Error(n.line, "'continue' used outside any loop");
    }

    void EmitWhile(const KisakAstNode& n) {
        if (n.children.size() < 2 || !n.children[0] || !n.children[1]) {
            Error(n.line, "malformed while statement"); return;
        }
        size_t top = here();
        EmitExpression(*n.children[0]);
        size_t jfalse = EmitJumpOnFalsePlaceholder();
        LoopOrSwitchContext ctx;
        ctx.kind = LoopOrSwitchKind::WhileLoop;
        ctx.whileCondTop = top;
        loopSwitchStack.push_back(std::move(ctx));
        EmitStatement(*n.children[1]);
        LoopOrSwitchContext finished = std::move(loopSwitchStack.back());
        loopSwitchStack.pop_back();
        EmitJumpBackTo(top, n.line);
        PatchForward16(jfalse, n.line);
        for (size_t at : finished.breakFixups) PatchForward32(at);
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
        LoopOrSwitchContext ctx;
        ctx.kind = LoopOrSwitchKind::ForLoop;
        loopSwitchStack.push_back(std::move(ctx));
        if (n.children[3]) EmitStatement(*n.children[3]);
        LoopOrSwitchContext finished = std::move(loopSwitchStack.back());
        loopSwitchStack.pop_back();
        // continue;'s forward-fixup resolution point: exactly HERE, between
        // the body and the increment clause -- matching real C semantics
        // (continue still runs the increment before the condition re-check).
        for (size_t at : finished.continueFixups) PatchForward32(at);
        if (n.children[2]) EmitExprClause(*n.children[2]);
        EmitJumpBackTo(condTop, n.line);
        if (hasCond) PatchForward16(jfalse, n.line);
        for (size_t at : finished.breakFixups) PatchForward32(at);
    }

    // Switch/loop-control blueprint step 3, JOIN: switch statement
    // desugaring via a SPLIT-DISPATCH layout (Architecture fact 3,
    // CRITICALLY corrected by adversarial review before any code was
    // written — an earlier draft interleaved each case's comparison
    // directly before its own body, which silently produces the OPPOSITE
    // of fallthrough; see the plan's own Plan-level notes for the full
    // trace). The layout:
    //   1. Evaluate the subject ONCE, store into the hidden slot (so
    //      `ally_sas_woodland_smg_mp5.gsc`'s call-expression subject,
    //      `codescripts\character::get_random_character(5)`, is never
    //      re-evaluated).
    //   2. Push a switch context (no continue-fixup mechanism) so break;
    //      inside any case body works via Step 2's shared mechanism, and
    //      continue; correctly searches PAST this context for an
    //      enclosing loop.
    //   3. DISPATCH PROLOGUE: one (EvalLocal, literal, OP_equality,
    //      OP_JumpOnTrue) comparison per non-default clause, in source
    //      order, each jumping to a forward-patched body label. After the
    //      last comparison, ONE unconditional OP_jump reaches either the
    //      default clause's body (wherever it sits in source order -- no
    //      special-casing needed for a non-last default) or the switch's
    //      own end if there is no default.
    //   4. BODY BLOCK: every clause's statements, contiguous, in source
    //      order, with NO jump inserted between clauses -- this omission
    //      IS the fallthrough (cargoship_extract.gsc:189's own headline
    //      construct: matching the first case runs every subsequent
    //      case's body too, since nothing jumps out early absent a
    //      `break;`).
    // OP_JumpOnTrue's operands are `uint16` (`PatchForward16`, max 0xFFFF,
    // matching every other forward-conditional-jump in this compiler) --
    // PatchForward16 already errors cleanly rather than corrupting
    // bytecode if a switch's own body block ever exceeds that bound, so
    // this is a recognized, understood limit, not a silent risk.
    void EmitSwitch(const KisakAstNode& n) {
        if (n.children.empty() || !n.children[0]) {
            Error(n.line, "malformed switch statement"); return;
        }
        auto slotIt = switchSlotNames.find(&n);
        if (slotIt == switchSlotNames.end()) {
            Error(n.line, "internal: switch statement has no pre-allocated hidden slot");
            return;
        }
        int cached = locals.Cached(slotIt->second);

        // Evaluate the subject once, store into the hidden slot.
        EmitExpression(*n.children[0]);
        EmitSetLocal(cached);

        LoopOrSwitchContext ctx;
        ctx.kind = LoopOrSwitchKind::Switch;
        loopSwitchStack.push_back(std::move(ctx));

        // Dispatch prologue: one comparison per non-default clause.
        std::vector<size_t> jumpTrueFixups(n.children.size(), 0);
        bool hasDefault = false;
        for (size_t i = 1; i < n.children.size(); ++i) {
            const KisakAstNode& clause = *n.children[i];
            if (clause.isDefault) {
                if (hasDefault) {
                    Error(n.line, "switch has more than one 'default:' clause");
                }
                hasDefault = true;
                continue;
            }
            if (clause.children.empty() || !clause.children[0]) {
                Error(n.line, "malformed case clause"); continue;
            }
            EmitEvalLocal(cached);
            EmitExpression(*clause.children[0]);  // the case's literal value
            EmitOp(Op::OP_equality);
            jumpTrueFixups[i] = EmitJumpOnTruePlaceholder();
        }
        // The single "no case matched" jump -- reaches default's body if
        // one exists (patched below, at whatever source position it sits
        // at), else falls through to Lend (patched after the body loop).
        size_t noMatchJumpAt = EmitJumpPlaceholder();

        // Body block: every clause's statements, contiguous, source order.
        for (size_t i = 1; i < n.children.size(); ++i) {
            const KisakAstNode& clause = *n.children[i];
            if (clause.isDefault) {
                PatchForward32(noMatchJumpAt);
            } else {
                PatchForward16(jumpTrueFixups[i], n.line);
            }
            size_t bodyStart = clause.isDefault ? 0 : 1;  // skip the value node for non-default clauses
            for (size_t k = bodyStart; k < clause.children.size(); ++k) {
                if (clause.children[k]) EmitStatement(*clause.children[k]);
            }
        }
        if (!hasDefault) {
            PatchForward32(noMatchJumpAt);  // no default -> falls straight to Lend
        }

        LoopOrSwitchContext finished = std::move(loopSwitchStack.back());
        loopSwitchStack.pop_back();
        for (size_t at : finished.breakFixups) PatchForward32(at);
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
        switchSlotNames.clear();  // per-function, like `locals` itself
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
