#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "kisak_script_entity_android.h"

// GScript VM data structures — blueprint plans/android-gscript-vm-port.md,
// step 2. Opcode enum + bytecode buffer reader primitives only: no
// execution logic yet (that's step 3's KisakScriptVm::Execute), no
// compiler (step 8). Nothing here is wired into the build's gameplay loop.
//
// Reference: src/script/scr_vm.h:16-157 (Opcode_t enum, 139 entries) and
// src/script/scr_vm.cpp:1637-1687 (Scr_ReadCodePos/Scr_ReadUnsigned/
// Scr_ReadInt/Scr_ReadUnsignedShort/Scr_ReadFloat/Scr_ReadVector — the
// actual VM_ExecuteInternal operand readers; NOT scr_readwrite.cpp, which
// is savegame serialization only).

// Matches src/script/scr_vm.h:16-157 exactly: names and numeric values.
// The retail enum's underlying type is __int32, but every on-wire opcode
// in scrVarPub.programBuffer is fetched as a single unsigned byte
// (`opcode = *(unsigned char*)fs.pos++`, scr_vm.cpp dispatch loop) — the
// wire format is byte-width, so this is uint8_t rather than int32_t.
enum class KisakScriptOpcode : uint8_t {
    OP_End = 0x00,
    OP_Return = 0x01,
    OP_GetUndefined = 0x02,
    OP_GetZero = 0x03,
    OP_GetByte = 0x04,
    OP_GetNegByte = 0x05,
    OP_GetUnsignedShort = 0x06,
    OP_GetNegUnsignedShort = 0x07,
    OP_GetInteger = 0x08,
    OP_GetFloat = 0x09,
    OP_GetString = 0x0A,
    OP_GetIString = 0x0B,
    OP_GetVector = 0x0C,
    OP_GetLevelObject = 0x0D,
    OP_GetAnimObject = 0x0E,
    OP_GetSelf = 0x0F,
    OP_GetLevel = 0x10,
    OP_GetGame = 0x11,
    OP_GetAnim = 0x12,
    OP_GetAnimation = 0x13,
    OP_GetGameRef = 0x14,
    OP_GetFunction = 0x15,
    OP_CreateLocalVariable = 0x16,
    OP_RemoveLocalVariables = 0x17,
    OP_EvalLocalVariableCached0 = 0x18,
    OP_EvalLocalVariableCached1 = 0x19,
    OP_EvalLocalVariableCached2 = 0x1A,
    OP_EvalLocalVariableCached3 = 0x1B,
    OP_EvalLocalVariableCached4 = 0x1C,
    OP_EvalLocalVariableCached5 = 0x1D,
    OP_EvalLocalVariableCached = 0x1E,
    OP_EvalLocalArrayCached = 0x1F,
    OP_EvalArray = 0x20,
    OP_EvalLocalArrayRefCached0 = 0x21,
    OP_EvalLocalArrayRefCached = 0x22,
    OP_EvalArrayRef = 0x23,
    OP_ClearArray = 0x24,
    OP_EmptyArray = 0x25,
    OP_GetSelfObject = 0x26,
    OP_EvalLevelFieldVariable = 0x27,
    OP_EvalAnimFieldVariable = 0x28,
    OP_EvalSelfFieldVariable = 0x29,
    OP_EvalFieldVariable = 0x2A,
    OP_EvalLevelFieldVariableRef = 0x2B,
    OP_EvalAnimFieldVariableRef = 0x2C,
    OP_EvalSelfFieldVariableRef = 0x2D,
    OP_EvalFieldVariableRef = 0x2E,
    OP_ClearFieldVariable = 0x2F,
    OP_SafeCreateVariableFieldCached = 0x30,
    OP_SafeSetVariableFieldCached0 = 0x31,
    OP_SafeSetVariableFieldCached = 0x32,
    OP_SafeSetWaittillVariableFieldCached = 0x33,
    OP_clearparams = 0x34,
    OP_checkclearparams = 0x35,
    OP_EvalLocalVariableRefCached0 = 0x36,
    OP_EvalLocalVariableRefCached = 0x37,
    OP_SetLevelFieldVariableField = 0x38,
    OP_SetVariableField = 0x39,
    OP_SetAnimFieldVariableField = 0x3A,
    OP_SetSelfFieldVariableField = 0x3B,
    OP_SetLocalVariableFieldCached0 = 0x3C,
    OP_SetLocalVariableFieldCached = 0x3D,
    OP_CallBuiltin0 = 0x3E,
    OP_CallBuiltin1 = 0x3F,
    OP_CallBuiltin2 = 0x40,
    OP_CallBuiltin3 = 0x41,
    OP_CallBuiltin4 = 0x42,
    OP_CallBuiltin5 = 0x43,
    OP_CallBuiltin = 0x44,
    OP_CallBuiltinMethod0 = 0x45,
    OP_CallBuiltinMethod1 = 0x46,
    OP_CallBuiltinMethod2 = 0x47,
    OP_CallBuiltinMethod3 = 0x48,
    OP_CallBuiltinMethod4 = 0x49,
    OP_CallBuiltinMethod5 = 0x4A,
    OP_CallBuiltinMethod = 0x4B,
    OP_wait = 0x4C,
    OP_waittillFrameEnd = 0x4D,
    OP_PreScriptCall = 0x4E,
    OP_ScriptFunctionCall2 = 0x4F,
    OP_ScriptFunctionCall = 0x50,
    OP_ScriptFunctionCallPointer = 0x51,
    OP_ScriptMethodCall = 0x52,
    OP_ScriptMethodCallPointer = 0x53,
    OP_ScriptThreadCall = 0x54,
    OP_ScriptThreadCallPointer = 0x55,
    OP_ScriptMethodThreadCall = 0x56,
    OP_ScriptMethodThreadCallPointer = 0x57,
    OP_DecTop = 0x58,
    OP_CastFieldObject = 0x59,
    OP_EvalLocalVariableObjectCached = 0x5A,
    OP_CastBool = 0x5B,
    OP_BoolNot = 0x5C,
    OP_BoolComplement = 0x5D,
    OP_JumpOnFalse = 0x5E,
    OP_JumpOnTrue = 0x5F,
    OP_JumpOnFalseExpr = 0x60,
    OP_JumpOnTrueExpr = 0x61,
    OP_jump = 0x62,
    OP_jumpback = 0x63,
    OP_inc = 0x64,
    OP_dec = 0x65,
    OP_bit_or = 0x66,
    OP_bit_ex_or = 0x67,
    OP_bit_and = 0x68,
    OP_equality = 0x69,
    OP_inequality = 0x6A,
    OP_less = 0x6B,
    OP_greater = 0x6C,
    OP_less_equal = 0x6D,
    OP_greater_equal = 0x6E,
    OP_shift_left = 0x6F,
    OP_shift_right = 0x70,
    OP_plus = 0x71,
    OP_minus = 0x72,
    OP_multiply = 0x73,
    OP_divide = 0x74,
    OP_mod = 0x75,
    OP_size = 0x76,
    OP_waittillmatch = 0x77,
    OP_waittill = 0x78,
    OP_notify = 0x79,
    OP_endon = 0x7A,
    OP_voidCodepos = 0x7B,
    OP_switch = 0x7C,
    OP_endswitch = 0x7D,
    OP_vector = 0x7E,
    OP_NOP = 0x7F,
    OP_abort = 0x80,
    OP_object = 0x81,
    OP_thread_object = 0x82,
    OP_EvalLocalVariable = 0x83,
    OP_EvalLocalVariableRef = 0x84,
    OP_prof_begin = 0x85,
    OP_prof_end = 0x86,
    OP_breakpoint = 0x87,
    OP_assignmentBreakpoint = 0x88,
    OP_manualAndAssignmentBreakpoint = 0x89,
    OP_count = 0x8A,
};

// A compiled script's flat bytecode buffer. Unlike the zone/asset loader
// (kisak_zone_loader_android.h), which MUST preserve retail's exact 32-bit
// serialized layout because it reads Activision's real shipped .ff files,
// this buffer is entirely our own artifact — step 8's from-scratch
// compiler both writes it and step 3's VM reads it, so there is no
// cross-format compatibility constraint on its internal representation.
// Kept minimal per the step 2 task list; step 3 extends this rather than
// this step over-designing it.
struct KisakScriptProgram {
    std::vector<uint8_t> bytecode;
    // Function name -> byte offset into bytecode of that function's first
    // instruction. Populated by the compiler (step 8); step 3's synthetic
    // test harness can also populate it by hand for hand-assembled tests.
    std::unordered_map<std::string, uint32_t> functionEntryPoints;
    // String literal pool: OP_GetString's 2-byte operand indexes into this.
    // A minimal stand-in for retail's global interned-string table (SL_*),
    // introduced in step 4 (not step 2/3) because it's the first step whose
    // own exit criteria need string literals at all (print/setdvar/getdvar
    // arguments) — step 6+'s real lexer/compiler will populate this the same
    // way a hand-assembled test does today; this is not meant to survive as
    // the long-term string representation once a proper intern table exists.
    std::vector<std::string> stringPool;
};

// Cursor over a KisakScriptProgram's bytecode buffer. Ports the reader
// primitives at src/script/scr_vm.cpp:1637-1687 (Scr_ReadCodePos etc.),
// adapted from retail's `const char**` pointer-cursor to a bounds-checked
// byte-offset cursor into a std::vector<uint8_t>.
//
// scr_vm.cpp's Scr_ReadCodePos reads sizeof(const char*) bytes and treats
// them as an absolute pointer back into the SAME in-memory bytecode buffer
// (used for OP_GetFunction / thread-call / return targets) — meaningful
// only because retail compiles+executes in the same 32-bit process. Since
// our compiler and VM likewise share one process and one buffer, but must
// not depend on host pointer width (this app also builds for 32-bit ABIs),
// KisakScriptCursor::ReadCodePos reads a fixed 4-byte byte-OFFSET into
// `bytecode` instead of a raw pointer — same role, host-width-independent.
struct KisakScriptCursor {
    const std::vector<uint8_t>* bytecode = nullptr;
    size_t pos = 0;

    uint8_t ReadByte();
    int8_t ReadSignedByte();          // OP_GetNegByte: -*(unsigned char*)pos++
    uint16_t ReadUnsignedShort();     // Scr_ReadUnsignedShort
    int32_t ReadInt();                // Scr_ReadInt
    float ReadFloat();                // Scr_ReadFloat
    // Returns the offset the 3 floats start at (caller reads bytecode[off..off+12)
    // directly) rather than copying, mirroring Scr_ReadVector's pointer-into-
    // buffer semantics without holding a dangling pointer past a vector resize.
    size_t ReadVector3();              // Scr_ReadVector
    uint32_t ReadCodePos();            // see class comment above
};

std::string DescribeScriptOpcode(KisakScriptOpcode opcode);

// ---------------------------------------------------------------------------
// Step 3: runtime value type + bytecode executor.
//
// Reference: VM_ExecuteInternal (src/script/scr_vm.cpp:2123-3479, the REAL main
// dispatch loop — NOT the two setjmp-guarded switches at 1823/1883, which are
// error-recovery stack cleanup) and the operator helpers in scr_variable.cpp
// (Scr_EvalPlus, Scr_CastWeakerPair, Scr_EvalEquality, ...). Only the entity-
// and builtin-free opcode subset is ported here; step 4 adds OP_CallBuiltin*,
// steps 8+ add entity fields / waittill / notify / switch / threading.
// ---------------------------------------------------------------------------

// GScript's runtime value type. This is a NEW type rather than a reuse of
// KisakExprValue (kisak_menu_expression_android.h). KisakExprValue is an
// Int/Float/String-only tagged struct built for the UI expression evaluator;
// it cannot represent (a) VAR_UNDEFINED, which GScript needs as a first-class
// runtime value (OP_GetUndefined, uninitialized locals, a function's default
// return), nor (b) the two VM-internal stack markers the call/return machinery
// relies on (a CodePos frame boundary and a PreCodePos argument delimiter).
// Bolting those onto KisakExprValue would leak VM-internal concerns into the
// unrelated UI evaluator. The two share a tagged-value philosophy but stay
// decoupled. An entity-ref variant is intentionally deferred to the step that
// first needs entities (step 5/9); adding it here would be dead weight.
enum class KisakScriptValueType : uint8_t {
    Undefined = 0,
    Int,
    Float,
    String,
    CodePos,     // internal marker: a script-function frame boundary on the stack
    PreCodePos,  // internal marker: delimits the start of a call's arguments
};

struct KisakScriptValue {
    KisakScriptValueType type = KisakScriptValueType::Undefined;
    int32_t i = 0;
    float f = 0.0f;
    std::string s;

    static KisakScriptValue Undefined();
    static KisakScriptValue Int(int32_t v);
    static KisakScriptValue Float(float v);
    static KisakScriptValue Str(std::string v);
    static KisakScriptValue Marker(KisakScriptValueType marker);

    bool IsNumeric() const { return type == KisakScriptValueType::Int ||
                                    type == KisakScriptValueType::Float; }
    // Mirrors Scr_CastBool: int -> (i != 0), float -> (f != 0). Callers must
    // check IsNumeric() first; non-numeric truthiness is a runtime error.
    bool Truthy() const;
    std::string Describe() const;   // debug-tagged, e.g. int(14) — logs/tests only
    std::string AsString() const;   // clean text form — print output, dvar values
};

enum class KisakScriptExecStatus : uint8_t {
    Completed,          // ran to a top-level OP_End/OP_Return
    RuntimeError,       // a script-level error (type mismatch, divide by zero, ...)
    UnsupportedOpcode,  // hit an opcode outside step 3's subset — stopped cleanly
    Aborted,            // OP_abort
};

struct KisakScriptExecResult {
    KisakScriptExecStatus status = KisakScriptExecStatus::Completed;
    KisakScriptValue returnValue;               // meaningful when status == Completed
    std::string message;                        // set for RuntimeError/UnsupportedOpcode
    size_t stopPos = 0;                          // bytecode offset execution stopped at
    KisakScriptOpcode stopOpcode = KisakScriptOpcode::OP_End;
};

// Sink for print/println-style output and diagnostics. nullptr routes to the
// platform default (__android_log_print on device, stderr on host). Host tests
// install their own sink to capture output.
using KisakScriptLogFn = void (*)(const std::string& line);

// The executor. Holds configuration only; all per-run state is local to
// Execute() so a single instance can be reused across programs.
//
// Error model (decided here so later steps inherit it): a plain result code +
// message, NOT setjmp/longjmp (retail's mechanism) and NOT C++ exceptions.
// Rationale — the reference longjmp path exists to unwind a persistent global
// VM stack and resume other script threads, neither of which this trimmed
// single-thread executor has; a status enum is the smallest thing that lets
// step 4/8 distinguish "script bug" from "opcode not implemented yet" without
// exceptions-as-control-flow, which this codebase avoids.
struct KisakScriptVm {
    KisakScriptExecResult Execute(const KisakScriptProgram& program, uint32_t entryOffset);

    KisakScriptLogFn logFn = nullptr;
    // Guards against a runaway loop in a malformed test program. Retail uses a
    // 5s wall-clock timeout (INFINITE_LOOP_TIMEOUT); a step budget is simpler
    // and deterministic for host tests.
    uint64_t maxSteps = 100000000ull;
};

// ---------------------------------------------------------------------------
// Step 4: builtin function dispatch (OP_CallBuiltin*) + a minimal builtin
// table: print, println, isdefined, isstring, isarray, getdvar, getdvarint,
// getdvarfloat, setdvar, assert, assertmsg — the lowest-dependency builtins
// identified by this plan's research (g_scr_main.cpp:328-370). Anything that
// pulls in entities/AI (spawn, getaiarray, ...) is explicitly out of scope
// until step 5+ provides an entity model.
//
// Reference: functions[251] (g_scr_main.cpp:324, BuiltinFunctionDef — a
// name + void(*)() pointer; the retail function reads its args off the VM's
// global top-of-stack via Scr_GetInt/Scr_GetString/etc and optionally pushes
// a return value via Scr_AddInt/Scr_AddString/etc) and the CallBuiltIn/
// post_builtin dispatch machinery (scr_vm.cpp:2606-2712 — see also the
// analogous case block starting ~1961, which is the OP_CallBuiltin path
// inside a DIFFERENT setjmp-guarded switch and not relevant here).
//
// Key retail facts this step preserves: argcount comes from the opcode
// variant (OP_CallBuiltin0..5 encode 0-5 directly; the generic OP_CallBuiltin
// reads a 1-byte count) with a 2-byte builtinIndex operand following in BOTH
// forms; Scr_GetInt(0)/Scr_GetString(0)/etc index args as top[-index], i.e.
// index 0 = the LAST-PUSHED argument = conventionally the first syntactic
// argument (so the compiler, step 8, must push arguments right-to-left —
// noted here since step 8 will need this exact convention); if the builtin
// doesn't push a return value, the VM pushes VAR_UNDEFINED so a builtin call
// always leaves exactly one value on the stack.
//
// Deliberate deviation: builtinIndex resolves through OUR OWN small table
// below (11 entries), NOT retail's combined 251+166-entry functions[]/
// methods[] tables — there is no reason to replicate retail's full index
// space for 11 builtins. Step 8's compiler must resolve builtin names to
// indices into KisakScriptBuiltinTable(), not retail's numbering.

// A builtin's view of its arguments: index 0 is the first syntactic
// argument (matches Scr_GetInt(0) etc — see class comment above), regardless
// of how the VM's internal value stack is ordered.
struct KisakScriptBuiltinArgs {
    const KisakScriptValue* values = nullptr;  // values[0] = first syntactic arg
    uint32_t count = 0;

    bool InRange(uint32_t index) const { return index < count; }
    // Out-of-range/wrong-type access returns Undefined rather than crashing;
    // builtins that require an argument (e.g. setdvar's name) explicitly
    // check IsRange/type and call KisakScriptBuiltinCall::Fail themselves —
    // mirrors Scr_GetString's own "parameter does not exist" script error,
    // just surfaced through this step's result-code error model instead of
    // longjmp.
    const KisakScriptValue& Get(uint32_t index) const;
};

// A single builtin invocation's context: its arguments, its return value
// (Undefined unless the builtin sets one — matches retail's post_builtin
// "push VAR_UNDEFINED when nothing was added" behavior), and a way to raise
// a script runtime error (this step's error model, not Scr_Error/longjmp).
struct KisakScriptBuiltinCall {
    KisakScriptBuiltinArgs args;
    KisakScriptValue returnValue;  // Undefined by default
    bool failed = false;
    std::string failMessage;

    void Fail(std::string message) {
        failed = true;
        failMessage = std::move(message);
    }
};

using KisakScriptBuiltinFn = void (*)(KisakScriptBuiltinCall& call, KisakScriptLogFn log);

struct KisakScriptBuiltinDef {
    const char* name;
    KisakScriptBuiltinFn fn;
};

// Fixed table, index-stable across a build (step 8's compiler embeds these
// indices into bytecode) — print=0, println=1, isdefined=2, isstring=3,
// isarray=4, getdvar=5, getdvarint=6, getdvarfloat=7, setdvar=8, assert=9,
// assertmsg=10, spawn=11 (step 9). Appending spawn at the end keeps every
// earlier index stable for already-compiled bytecode.
const std::vector<KisakScriptBuiltinDef>& KisakScriptBuiltinTable();

// -1 if no builtin has this name.
int KisakScriptFindBuiltinIndex(const std::string& name);

// ---------------------------------------------------------------------------
// Step 9: `spawn` builtin state. A plain global, matching this codebase's
// established pattern for builtin-adjacent shared state (the menu dvar store,
// kisak_menu_expression_android.h) — KisakScriptBuiltinFn is a plain function
// pointer with no per-call context/capture, so a global is the only option
// without widening every existing builtin's signature for the sake of one.
//
// spawn(classname, x, y, z) — a deliberately minimal stand-in for real GSC's
// spawn(classname, origin): this trimmed grammar has no vector-literal syntax
// (step 7 never parses `(x, y, z)` as a 3-component literal — distinguishing
// it from a parenthesized sub-expression needs comma-counting lookahead this
// subset didn't build), so origin is 3 separate numeric args instead. Builds
// a KisakScriptEntity (step 5) and returns an opaque Int handle (its index in
// the run-scoped list) — there is still no entity value type in
// KisakScriptValue, so a script can hold the handle or pass it around but
// cannot call methods or access fields on it (the same deferred entity-model
// gap step 8 already documented).
void ResetKisakScriptSpawnedEntities();
const std::vector<KisakScriptEntity>& GetKisakScriptSpawnedEntities();
