#include "kisak_script_vm_android.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace {

// Bounds check matching this project's convention (see e.g.
// kisak_zone_rawfile_android.cpp's scan): out-of-range reads are a bug in
// the compiler that produced this buffer (step 8) or in a hand-assembled
// test program, not attacker-controlled input — but this file has no
// caller yet (step 3 wires it up), so failing loudly now is cheap
// insurance against a silent out-of-bounds read once it does.
void RequireBytes(const KisakScriptCursor& cursor, size_t count) {
    if (cursor.bytecode == nullptr ||
        cursor.pos + count > cursor.bytecode->size()) {
        __builtin_trap();
    }
}

}  // namespace

uint8_t KisakScriptCursor::ReadByte() {
    RequireBytes(*this, 1);
    uint8_t value = (*bytecode)[pos];
    pos += 1;
    return value;
}

int8_t KisakScriptCursor::ReadSignedByte() {
    return -static_cast<int8_t>(ReadByte());
}

uint16_t KisakScriptCursor::ReadUnsignedShort() {
    RequireBytes(*this, 2);
    uint16_t value;
    std::memcpy(&value, bytecode->data() + pos, sizeof(value));
    pos += sizeof(value);
    return value;
}

int32_t KisakScriptCursor::ReadInt() {
    RequireBytes(*this, 4);
    int32_t value;
    std::memcpy(&value, bytecode->data() + pos, sizeof(value));
    pos += sizeof(value);
    return value;
}

float KisakScriptCursor::ReadFloat() {
    RequireBytes(*this, 4);
    float value;
    std::memcpy(&value, bytecode->data() + pos, sizeof(value));
    pos += sizeof(value);
    return value;
}

size_t KisakScriptCursor::ReadVector3() {
    RequireBytes(*this, 12);
    size_t start = pos;
    pos += 12;
    return start;
}

uint32_t KisakScriptCursor::ReadCodePos() {
    RequireBytes(*this, 4);
    uint32_t value;
    std::memcpy(&value, bytecode->data() + pos, sizeof(value));
    pos += sizeof(value);
    return value;
}

std::string DescribeScriptOpcode(KisakScriptOpcode opcode) {
    switch (opcode) {
        case KisakScriptOpcode::OP_End: return "OP_End";
        case KisakScriptOpcode::OP_Return: return "OP_Return";
        case KisakScriptOpcode::OP_GetUndefined: return "OP_GetUndefined";
        case KisakScriptOpcode::OP_GetZero: return "OP_GetZero";
        case KisakScriptOpcode::OP_GetByte: return "OP_GetByte";
        case KisakScriptOpcode::OP_GetNegByte: return "OP_GetNegByte";
        case KisakScriptOpcode::OP_GetUnsignedShort: return "OP_GetUnsignedShort";
        case KisakScriptOpcode::OP_GetNegUnsignedShort: return "OP_GetNegUnsignedShort";
        case KisakScriptOpcode::OP_GetInteger: return "OP_GetInteger";
        case KisakScriptOpcode::OP_GetFloat: return "OP_GetFloat";
        case KisakScriptOpcode::OP_GetString: return "OP_GetString";
        case KisakScriptOpcode::OP_GetIString: return "OP_GetIString";
        case KisakScriptOpcode::OP_GetVector: return "OP_GetVector";
        case KisakScriptOpcode::OP_GetLevelObject: return "OP_GetLevelObject";
        case KisakScriptOpcode::OP_GetAnimObject: return "OP_GetAnimObject";
        case KisakScriptOpcode::OP_GetSelf: return "OP_GetSelf";
        case KisakScriptOpcode::OP_GetLevel: return "OP_GetLevel";
        case KisakScriptOpcode::OP_GetGame: return "OP_GetGame";
        case KisakScriptOpcode::OP_GetAnim: return "OP_GetAnim";
        case KisakScriptOpcode::OP_GetAnimation: return "OP_GetAnimation";
        case KisakScriptOpcode::OP_GetGameRef: return "OP_GetGameRef";
        case KisakScriptOpcode::OP_GetFunction: return "OP_GetFunction";
        case KisakScriptOpcode::OP_CreateLocalVariable: return "OP_CreateLocalVariable";
        case KisakScriptOpcode::OP_RemoveLocalVariables: return "OP_RemoveLocalVariables";
        case KisakScriptOpcode::OP_EvalLocalVariableCached0: return "OP_EvalLocalVariableCached0";
        case KisakScriptOpcode::OP_EvalLocalVariableCached1: return "OP_EvalLocalVariableCached1";
        case KisakScriptOpcode::OP_EvalLocalVariableCached2: return "OP_EvalLocalVariableCached2";
        case KisakScriptOpcode::OP_EvalLocalVariableCached3: return "OP_EvalLocalVariableCached3";
        case KisakScriptOpcode::OP_EvalLocalVariableCached4: return "OP_EvalLocalVariableCached4";
        case KisakScriptOpcode::OP_EvalLocalVariableCached5: return "OP_EvalLocalVariableCached5";
        case KisakScriptOpcode::OP_EvalLocalVariableCached: return "OP_EvalLocalVariableCached";
        case KisakScriptOpcode::OP_EvalLocalArrayCached: return "OP_EvalLocalArrayCached";
        case KisakScriptOpcode::OP_EvalArray: return "OP_EvalArray";
        case KisakScriptOpcode::OP_EvalLocalArrayRefCached0: return "OP_EvalLocalArrayRefCached0";
        case KisakScriptOpcode::OP_EvalLocalArrayRefCached: return "OP_EvalLocalArrayRefCached";
        case KisakScriptOpcode::OP_EvalArrayRef: return "OP_EvalArrayRef";
        case KisakScriptOpcode::OP_ClearArray: return "OP_ClearArray";
        case KisakScriptOpcode::OP_EmptyArray: return "OP_EmptyArray";
        case KisakScriptOpcode::OP_GetSelfObject: return "OP_GetSelfObject";
        case KisakScriptOpcode::OP_EvalLevelFieldVariable: return "OP_EvalLevelFieldVariable";
        case KisakScriptOpcode::OP_EvalAnimFieldVariable: return "OP_EvalAnimFieldVariable";
        case KisakScriptOpcode::OP_EvalSelfFieldVariable: return "OP_EvalSelfFieldVariable";
        case KisakScriptOpcode::OP_EvalFieldVariable: return "OP_EvalFieldVariable";
        case KisakScriptOpcode::OP_EvalLevelFieldVariableRef: return "OP_EvalLevelFieldVariableRef";
        case KisakScriptOpcode::OP_EvalAnimFieldVariableRef: return "OP_EvalAnimFieldVariableRef";
        case KisakScriptOpcode::OP_EvalSelfFieldVariableRef: return "OP_EvalSelfFieldVariableRef";
        case KisakScriptOpcode::OP_EvalFieldVariableRef: return "OP_EvalFieldVariableRef";
        case KisakScriptOpcode::OP_ClearFieldVariable: return "OP_ClearFieldVariable";
        case KisakScriptOpcode::OP_SafeCreateVariableFieldCached: return "OP_SafeCreateVariableFieldCached";
        case KisakScriptOpcode::OP_SafeSetVariableFieldCached0: return "OP_SafeSetVariableFieldCached0";
        case KisakScriptOpcode::OP_SafeSetVariableFieldCached: return "OP_SafeSetVariableFieldCached";
        case KisakScriptOpcode::OP_SafeSetWaittillVariableFieldCached: return "OP_SafeSetWaittillVariableFieldCached";
        case KisakScriptOpcode::OP_clearparams: return "OP_clearparams";
        case KisakScriptOpcode::OP_checkclearparams: return "OP_checkclearparams";
        case KisakScriptOpcode::OP_EvalLocalVariableRefCached0: return "OP_EvalLocalVariableRefCached0";
        case KisakScriptOpcode::OP_EvalLocalVariableRefCached: return "OP_EvalLocalVariableRefCached";
        case KisakScriptOpcode::OP_SetLevelFieldVariableField: return "OP_SetLevelFieldVariableField";
        case KisakScriptOpcode::OP_SetVariableField: return "OP_SetVariableField";
        case KisakScriptOpcode::OP_SetAnimFieldVariableField: return "OP_SetAnimFieldVariableField";
        case KisakScriptOpcode::OP_SetSelfFieldVariableField: return "OP_SetSelfFieldVariableField";
        case KisakScriptOpcode::OP_SetLocalVariableFieldCached0: return "OP_SetLocalVariableFieldCached0";
        case KisakScriptOpcode::OP_SetLocalVariableFieldCached: return "OP_SetLocalVariableFieldCached";
        case KisakScriptOpcode::OP_CallBuiltin0: return "OP_CallBuiltin0";
        case KisakScriptOpcode::OP_CallBuiltin1: return "OP_CallBuiltin1";
        case KisakScriptOpcode::OP_CallBuiltin2: return "OP_CallBuiltin2";
        case KisakScriptOpcode::OP_CallBuiltin3: return "OP_CallBuiltin3";
        case KisakScriptOpcode::OP_CallBuiltin4: return "OP_CallBuiltin4";
        case KisakScriptOpcode::OP_CallBuiltin5: return "OP_CallBuiltin5";
        case KisakScriptOpcode::OP_CallBuiltin: return "OP_CallBuiltin";
        case KisakScriptOpcode::OP_CallBuiltinMethod0: return "OP_CallBuiltinMethod0";
        case KisakScriptOpcode::OP_CallBuiltinMethod1: return "OP_CallBuiltinMethod1";
        case KisakScriptOpcode::OP_CallBuiltinMethod2: return "OP_CallBuiltinMethod2";
        case KisakScriptOpcode::OP_CallBuiltinMethod3: return "OP_CallBuiltinMethod3";
        case KisakScriptOpcode::OP_CallBuiltinMethod4: return "OP_CallBuiltinMethod4";
        case KisakScriptOpcode::OP_CallBuiltinMethod5: return "OP_CallBuiltinMethod5";
        case KisakScriptOpcode::OP_CallBuiltinMethod: return "OP_CallBuiltinMethod";
        case KisakScriptOpcode::OP_wait: return "OP_wait";
        case KisakScriptOpcode::OP_waittillFrameEnd: return "OP_waittillFrameEnd";
        case KisakScriptOpcode::OP_PreScriptCall: return "OP_PreScriptCall";
        case KisakScriptOpcode::OP_ScriptFunctionCall2: return "OP_ScriptFunctionCall2";
        case KisakScriptOpcode::OP_ScriptFunctionCall: return "OP_ScriptFunctionCall";
        case KisakScriptOpcode::OP_ScriptFunctionCallPointer: return "OP_ScriptFunctionCallPointer";
        case KisakScriptOpcode::OP_ScriptMethodCall: return "OP_ScriptMethodCall";
        case KisakScriptOpcode::OP_ScriptMethodCallPointer: return "OP_ScriptMethodCallPointer";
        case KisakScriptOpcode::OP_ScriptThreadCall: return "OP_ScriptThreadCall";
        case KisakScriptOpcode::OP_ScriptThreadCallPointer: return "OP_ScriptThreadCallPointer";
        case KisakScriptOpcode::OP_ScriptMethodThreadCall: return "OP_ScriptMethodThreadCall";
        case KisakScriptOpcode::OP_ScriptMethodThreadCallPointer: return "OP_ScriptMethodThreadCallPointer";
        case KisakScriptOpcode::OP_DecTop: return "OP_DecTop";
        case KisakScriptOpcode::OP_CastFieldObject: return "OP_CastFieldObject";
        case KisakScriptOpcode::OP_EvalLocalVariableObjectCached: return "OP_EvalLocalVariableObjectCached";
        case KisakScriptOpcode::OP_CastBool: return "OP_CastBool";
        case KisakScriptOpcode::OP_BoolNot: return "OP_BoolNot";
        case KisakScriptOpcode::OP_BoolComplement: return "OP_BoolComplement";
        case KisakScriptOpcode::OP_JumpOnFalse: return "OP_JumpOnFalse";
        case KisakScriptOpcode::OP_JumpOnTrue: return "OP_JumpOnTrue";
        case KisakScriptOpcode::OP_JumpOnFalseExpr: return "OP_JumpOnFalseExpr";
        case KisakScriptOpcode::OP_JumpOnTrueExpr: return "OP_JumpOnTrueExpr";
        case KisakScriptOpcode::OP_jump: return "OP_jump";
        case KisakScriptOpcode::OP_jumpback: return "OP_jumpback";
        case KisakScriptOpcode::OP_inc: return "OP_inc";
        case KisakScriptOpcode::OP_dec: return "OP_dec";
        case KisakScriptOpcode::OP_bit_or: return "OP_bit_or";
        case KisakScriptOpcode::OP_bit_ex_or: return "OP_bit_ex_or";
        case KisakScriptOpcode::OP_bit_and: return "OP_bit_and";
        case KisakScriptOpcode::OP_equality: return "OP_equality";
        case KisakScriptOpcode::OP_inequality: return "OP_inequality";
        case KisakScriptOpcode::OP_less: return "OP_less";
        case KisakScriptOpcode::OP_greater: return "OP_greater";
        case KisakScriptOpcode::OP_less_equal: return "OP_less_equal";
        case KisakScriptOpcode::OP_greater_equal: return "OP_greater_equal";
        case KisakScriptOpcode::OP_shift_left: return "OP_shift_left";
        case KisakScriptOpcode::OP_shift_right: return "OP_shift_right";
        case KisakScriptOpcode::OP_plus: return "OP_plus";
        case KisakScriptOpcode::OP_minus: return "OP_minus";
        case KisakScriptOpcode::OP_multiply: return "OP_multiply";
        case KisakScriptOpcode::OP_divide: return "OP_divide";
        case KisakScriptOpcode::OP_mod: return "OP_mod";
        case KisakScriptOpcode::OP_size: return "OP_size";
        case KisakScriptOpcode::OP_waittillmatch: return "OP_waittillmatch";
        case KisakScriptOpcode::OP_waittill: return "OP_waittill";
        case KisakScriptOpcode::OP_notify: return "OP_notify";
        case KisakScriptOpcode::OP_endon: return "OP_endon";
        case KisakScriptOpcode::OP_voidCodepos: return "OP_voidCodepos";
        case KisakScriptOpcode::OP_switch: return "OP_switch";
        case KisakScriptOpcode::OP_endswitch: return "OP_endswitch";
        case KisakScriptOpcode::OP_vector: return "OP_vector";
        case KisakScriptOpcode::OP_NOP: return "OP_NOP";
        case KisakScriptOpcode::OP_abort: return "OP_abort";
        case KisakScriptOpcode::OP_object: return "OP_object";
        case KisakScriptOpcode::OP_thread_object: return "OP_thread_object";
        case KisakScriptOpcode::OP_EvalLocalVariable: return "OP_EvalLocalVariable";
        case KisakScriptOpcode::OP_EvalLocalVariableRef: return "OP_EvalLocalVariableRef";
        case KisakScriptOpcode::OP_prof_begin: return "OP_prof_begin";
        case KisakScriptOpcode::OP_prof_end: return "OP_prof_end";
        case KisakScriptOpcode::OP_breakpoint: return "OP_breakpoint";
        case KisakScriptOpcode::OP_assignmentBreakpoint: return "OP_assignmentBreakpoint";
        case KisakScriptOpcode::OP_manualAndAssignmentBreakpoint: return "OP_manualAndAssignmentBreakpoint";
        case KisakScriptOpcode::OP_count: return "OP_count";
    }
    return "OP_<unknown>";
}

// ---------------------------------------------------------------------------
// Step 3: runtime value type + executor.
// ---------------------------------------------------------------------------

KisakScriptValue KisakScriptValue::Undefined() {
    return KisakScriptValue{};
}

KisakScriptValue KisakScriptValue::Int(int32_t v) {
    KisakScriptValue out;
    out.type = KisakScriptValueType::Int;
    out.i = v;
    return out;
}

KisakScriptValue KisakScriptValue::Float(float v) {
    KisakScriptValue out;
    out.type = KisakScriptValueType::Float;
    out.f = v;
    return out;
}

KisakScriptValue KisakScriptValue::Str(std::string v) {
    KisakScriptValue out;
    out.type = KisakScriptValueType::String;
    out.s = std::move(v);
    return out;
}

KisakScriptValue KisakScriptValue::Marker(KisakScriptValueType marker) {
    KisakScriptValue out;
    out.type = marker;
    return out;
}

bool KisakScriptValue::Truthy() const {
    switch (type) {
        case KisakScriptValueType::Int: return i != 0;
        case KisakScriptValueType::Float: return f != 0.0f;
        default: return false;
    }
}

std::string KisakScriptValue::Describe() const {
    char buf[64];
    switch (type) {
        case KisakScriptValueType::Undefined: return "undefined";
        case KisakScriptValueType::Int:
            std::snprintf(buf, sizeof(buf), "int(%d)", i);
            return buf;
        case KisakScriptValueType::Float:
            std::snprintf(buf, sizeof(buf), "float(%g)", f);
            return buf;
        case KisakScriptValueType::String: return "string(\"" + s + "\")";
        case KisakScriptValueType::CodePos: return "<codepos>";
        case KisakScriptValueType::PreCodePos: return "<precodepos>";
    }
    return "<?>";
}

namespace {

const char* const kLogTag = "KisakCODAndroid";

void DefaultLog(const std::string& line) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", line.c_str());
#else
    std::fprintf(stderr, "[%s] %s\n", kLogTag, line.c_str());
#endif
}

// A single called-into script function's activation: its return cursor and its
// own local-variable slots. Locals are addressed newest-first to match retail's
// scrVmPub.localVars scheme, where OP_EvalLocalVariableCached0 reads the most
// recently created local (localVars[0]) and cached index N reads localVars[-N].
struct Frame {
    size_t returnPos = 0;                    // caller cursor position to resume at
    std::vector<KisakScriptValue> locals;
    int refSlot = -1;                        // absolute index of the current lvalue ref
};

// The whole executor lives here so the opcode handlers can share stack/frame/
// error state without threading a dozen references through every helper.
struct Interpreter {
    const KisakScriptProgram& program;
    KisakScriptCursor cursor;
    std::vector<KisakScriptValue> stack;
    std::vector<Frame> frames;
    KisakScriptExecResult result;
    bool halted = false;
    KisakScriptLogFn logFn = nullptr;
    uint64_t maxSteps = 0;

    explicit Interpreter(const KisakScriptProgram& prog) : program(prog) {
        cursor.bytecode = &prog.bytecode;
    }

    Frame& frame() { return frames.back(); }

    KisakScriptValue& top() { return stack.back(); }
    void push(KisakScriptValue v) { stack.push_back(std::move(v)); }
    KisakScriptValue pop() {
        KisakScriptValue v = std::move(stack.back());
        stack.pop_back();
        return v;
    }

    void Fail(KisakScriptExecStatus status, const std::string& msg) {
        halted = true;
        result.status = status;
        result.message = msg;
        result.stopPos = cursor.pos;
    }

    void RuntimeError(const std::string& msg) { Fail(KisakScriptExecStatus::RuntimeError, msg); }

    // Local slot addressing: cached index N -> locals[size - 1 - N].
    int SlotIndex(uint32_t cached) const {
        return static_cast<int>(frames.back().locals.size()) - 1 - static_cast<int>(cached);
    }
    bool SlotValid(int abs) const {
        return abs >= 0 && abs < static_cast<int>(frames.back().locals.size());
    }

    // Scr_CastWeakerPair: promote the weaker of an int/float pair to float so a
    // binary numeric op sees matching types. Non-numeric operands are a runtime
    // error (vector/string arithmetic beyond string concat is out of subset).
    bool CastNumericPair(KisakScriptValue& a, KisakScriptValue& b, const char* opName) {
        if (!a.IsNumeric() || !b.IsNumeric()) {
            RuntimeError(std::string(opName) + ": operands must be numeric (got " +
                         a.Describe() + ", " + b.Describe() + ")");
            return false;
        }
        if (a.type != b.type) {
            if (a.type == KisakScriptValueType::Int) {
                a = KisakScriptValue::Float(static_cast<float>(a.i));
            } else {
                b = KisakScriptValue::Float(static_cast<float>(b.i));
            }
        }
        return true;
    }

    void RunPlus(KisakScriptValue& a, KisakScriptValue& b) {
        if (a.type == KisakScriptValueType::String &&
            b.type == KisakScriptValueType::String) {
            a = KisakScriptValue::Str(a.s + b.s);
            return;
        }
        if (!CastNumericPair(a, b, "+")) return;
        if (a.type == KisakScriptValueType::Int) a.i += b.i;
        else a.f += b.f;
    }
    void RunMinus(KisakScriptValue& a, KisakScriptValue& b) {
        if (!CastNumericPair(a, b, "-")) return;
        if (a.type == KisakScriptValueType::Int) a.i -= b.i; else a.f -= b.f;
    }
    void RunMultiply(KisakScriptValue& a, KisakScriptValue& b) {
        if (!CastNumericPair(a, b, "*")) return;
        if (a.type == KisakScriptValueType::Int) a.i *= b.i; else a.f *= b.f;
    }
    // Scr_EvalDivide: integer division promotes to float (retail behaviour).
    void RunDivide(KisakScriptValue& a, KisakScriptValue& b) {
        if (!CastNumericPair(a, b, "/")) return;
        if (a.type == KisakScriptValueType::Int) {
            if (b.i == 0) { RuntimeError("divide by 0"); return; }
            a = KisakScriptValue::Float(static_cast<float>(
                static_cast<double>(a.i) / static_cast<double>(b.i)));
        } else {
            if (b.f == 0.0f) { RuntimeError("divide by 0"); return; }
            a.f /= b.f;
        }
    }
    void RunMod(KisakScriptValue& a, KisakScriptValue& b) {
        if (a.type != KisakScriptValueType::Int || b.type != KisakScriptValueType::Int) {
            RuntimeError("% requires two ints"); return;
        }
        if (b.i == 0) { RuntimeError("mod by 0"); return; }
        a.i %= b.i;
    }
    void RunBitwise(KisakScriptValue& a, KisakScriptValue& b, char kind) {
        if (a.type != KisakScriptValueType::Int || b.type != KisakScriptValueType::Int) {
            RuntimeError(std::string("bitwise op requires two ints (got ") +
                         a.Describe() + ", " + b.Describe() + ")");
            return;
        }
        switch (kind) {
            case '|': a.i |= b.i; break;
            case '^': a.i ^= b.i; break;
            case '&': a.i &= b.i; break;
            case '<': a.i <<= b.i; break;
            case '>': a.i >>= b.i; break;
        }
    }
    // Scr_EvalLess/Greater on a promoted pair; result is int 0/1.
    void RunCompare(KisakScriptValue& a, KisakScriptValue& b, char kind) {
        if (!CastNumericPair(a, b, "comparison")) return;
        bool r = false;
        if (a.type == KisakScriptValueType::Int) {
            switch (kind) {
                case '<': r = a.i < b.i; break;
                case '>': r = a.i > b.i; break;
                case 'l': r = a.i <= b.i; break;  // <=
                case 'g': r = a.i >= b.i; break;  // >=
            }
        } else {
            switch (kind) {
                case '<': r = a.f < b.f; break;
                case '>': r = a.f > b.f; break;
                case 'l': r = a.f <= b.f; break;
                case 'g': r = a.f >= b.f; break;
            }
        }
        a = KisakScriptValue::Int(r ? 1 : 0);
    }
    // Scr_EvalEquality: undefined==undefined is true; floats compared with the
    // retail 1e-6 epsilon; strings by content; ints/... by value.
    void RunEquality(KisakScriptValue& a, KisakScriptValue& b, bool wantEqual) {
        bool eq;
        if (a.type == KisakScriptValueType::Undefined ||
            b.type == KisakScriptValueType::Undefined) {
            eq = a.type == b.type;
        } else if (a.type == KisakScriptValueType::String ||
                   b.type == KisakScriptValueType::String) {
            if (a.type != b.type) { RuntimeError("== type mismatch"); return; }
            eq = a.s == b.s;
        } else if (a.IsNumeric() && b.IsNumeric()) {
            if (!CastNumericPair(a, b, "==")) return;
            if (a.type == KisakScriptValueType::Int) eq = a.i == b.i;
            else eq = std::fabs(a.f - b.f) < 0.0000009999999974752427;
        } else {
            RuntimeError("== unsupported operand types"); return;
        }
        a = KisakScriptValue::Int((eq == wantEqual) ? 1 : 0);
    }
    void RunCastBool(KisakScriptValue& v) {
        if (v.type == KisakScriptValueType::Int) {
            v.i = (v.i != 0) ? 1 : 0;
        } else if (v.type == KisakScriptValueType::Float) {
            v = KisakScriptValue::Int((v.f != 0.0f) ? 1 : 0);
        } else {
            RuntimeError("cannot cast " + v.Describe() + " to bool");
        }
    }

    void Run();
};

void Interpreter::Run() {
    uint64_t steps = 0;
    while (!halted) {
        if (++steps > maxSteps) {
            RuntimeError("step budget exceeded (possible infinite loop)");
            return;
        }
        if (cursor.pos >= program.bytecode.size()) {
            RuntimeError("instruction pointer ran off the end of the bytecode");
            return;
        }
        size_t opPos = cursor.pos;
        KisakScriptOpcode opcode = static_cast<KisakScriptOpcode>(cursor.ReadByte());

        switch (opcode) {
            // ---- value push ----
            case KisakScriptOpcode::OP_GetUndefined:
                push(KisakScriptValue::Undefined());
                break;
            case KisakScriptOpcode::OP_GetZero:
                push(KisakScriptValue::Int(0));
                break;
            case KisakScriptOpcode::OP_GetByte:
                push(KisakScriptValue::Int(cursor.ReadByte()));
                break;
            case KisakScriptOpcode::OP_GetNegByte:
                push(KisakScriptValue::Int(-static_cast<int32_t>(cursor.ReadByte())));
                break;
            case KisakScriptOpcode::OP_GetUnsignedShort:
                push(KisakScriptValue::Int(cursor.ReadUnsignedShort()));
                break;
            case KisakScriptOpcode::OP_GetNegUnsignedShort:
                push(KisakScriptValue::Int(-static_cast<int32_t>(cursor.ReadUnsignedShort())));
                break;
            case KisakScriptOpcode::OP_GetInteger:
                push(KisakScriptValue::Int(cursor.ReadInt()));
                break;
            case KisakScriptOpcode::OP_GetFloat:
                push(KisakScriptValue::Float(cursor.ReadFloat()));
                break;
            // Retail encodes strings as a 2-byte string-table id; this trimmed
            // VM has no string table yet, so a compiled program can't reach here.
            // Hand-assembled tests push strings via the assembler's literal pool
            // (encoded as the id, resolved by the harness), but the core VM keeps
            // the id as the int payload of a String only when a table exists.
            // Until step 6+ provides one, OP_GetString is out of subset.
            case KisakScriptOpcode::OP_GetString:
                Fail(KisakScriptExecStatus::UnsupportedOpcode,
                     "OP_GetString needs a string table (step 6+)");
                result.stopPos = opPos;
                result.stopOpcode = opcode;
                return;

            // ---- locals ----
            case KisakScriptOpcode::OP_CreateLocalVariable:
                cursor.ReadUnsignedShort();  // name id (unused in this trimmed model)
                frame().locals.push_back(KisakScriptValue::Undefined());
                break;
            case KisakScriptOpcode::OP_RemoveLocalVariables: {
                uint8_t n = cursor.ReadByte();
                auto& locals = frame().locals;
                if (n > locals.size()) { RuntimeError("OP_RemoveLocalVariables underflow"); return; }
                locals.resize(locals.size() - n);
                break;
            }
            case KisakScriptOpcode::OP_EvalLocalVariableCached0:
            case KisakScriptOpcode::OP_EvalLocalVariableCached1:
            case KisakScriptOpcode::OP_EvalLocalVariableCached2:
            case KisakScriptOpcode::OP_EvalLocalVariableCached3:
            case KisakScriptOpcode::OP_EvalLocalVariableCached4:
            case KisakScriptOpcode::OP_EvalLocalVariableCached5: {
                uint32_t cached = static_cast<uint8_t>(opcode) -
                    static_cast<uint8_t>(KisakScriptOpcode::OP_EvalLocalVariableCached0);
                int abs = SlotIndex(cached);
                if (!SlotValid(abs)) { RuntimeError("local cache index out of range"); return; }
                push(frame().locals[abs]);
                break;
            }
            case KisakScriptOpcode::OP_EvalLocalVariableCached: {
                uint32_t cached = cursor.ReadByte();
                int abs = SlotIndex(cached);
                if (!SlotValid(abs)) { RuntimeError("local cache index out of range"); return; }
                push(frame().locals[abs]);
                break;
            }
            case KisakScriptOpcode::OP_SetLocalVariableFieldCached0: {
                int abs = SlotIndex(0);
                if (!SlotValid(abs)) { RuntimeError("set local0 out of range"); return; }
                frame().locals[abs] = pop();
                break;
            }
            case KisakScriptOpcode::OP_SetLocalVariableFieldCached: {
                uint32_t cached = cursor.ReadByte();
                int abs = SlotIndex(cached);
                if (!SlotValid(abs)) { RuntimeError("set local out of range"); return; }
                frame().locals[abs] = pop();
                break;
            }
            case KisakScriptOpcode::OP_EvalLocalVariableRefCached0:
                frame().refSlot = SlotIndex(0);
                if (!SlotValid(frame().refSlot)) { RuntimeError("ref local0 out of range"); return; }
                break;
            case KisakScriptOpcode::OP_EvalLocalVariableRefCached: {
                uint32_t cached = cursor.ReadByte();
                frame().refSlot = SlotIndex(cached);
                if (!SlotValid(frame().refSlot)) { RuntimeError("ref local out of range"); return; }
                break;
            }
            case KisakScriptOpcode::OP_SetVariableField: {
                if (!SlotValid(frame().refSlot)) { RuntimeError("OP_SetVariableField without a ref"); return; }
                frame().locals[frame().refSlot] = pop();
                break;
            }

            // ---- parameter binding / call prologue ----
            case KisakScriptOpcode::OP_PreScriptCall:
            case KisakScriptOpcode::OP_voidCodepos:
                push(KisakScriptValue::Marker(KisakScriptValueType::PreCodePos));
                break;
            case KisakScriptOpcode::OP_SafeCreateVariableFieldCached: {
                cursor.ReadUnsignedShort();  // name id
                frame().locals.push_back(KisakScriptValue::Undefined());
                if (top().type != KisakScriptValueType::PreCodePos) {
                    frame().locals.back() = pop();
                }
                break;
            }
            case KisakScriptOpcode::OP_SafeSetVariableFieldCached0: {
                if (top().type != KisakScriptValueType::PreCodePos) {
                    int abs = SlotIndex(0);
                    if (!SlotValid(abs)) { RuntimeError("safe set local0 out of range"); return; }
                    frame().locals[abs] = pop();
                }
                break;
            }
            case KisakScriptOpcode::OP_checkclearparams:
                if (top().type != KisakScriptValueType::PreCodePos) {
                    RuntimeError("function called with too many parameters");
                    return;
                }
                top().type = KisakScriptValueType::CodePos;
                break;
            case KisakScriptOpcode::OP_clearparams:
                while (top().type != KisakScriptValueType::CodePos) {
                    if (stack.empty()) { RuntimeError("OP_clearparams underflow"); return; }
                    pop();
                }
                break;

            // ---- script function call / return ----
            case KisakScriptOpcode::OP_ScriptFunctionCall2:
                push(KisakScriptValue::Marker(KisakScriptValueType::PreCodePos));
                [[fallthrough]];
            case KisakScriptOpcode::OP_ScriptFunctionCall: {
                uint32_t entry = cursor.ReadCodePos();
                if (frames.size() >= 32) { RuntimeError("script stack overflow"); return; }
                Frame callee;
                callee.returnPos = cursor.pos;
                frames.push_back(std::move(callee));
                cursor.pos = entry;
                break;
            }
            case KisakScriptOpcode::OP_Return: {
                KisakScriptValue ret = pop();
                while (top().type != KisakScriptValueType::CodePos) {
                    if (stack.empty()) { RuntimeError("OP_Return stack underflow"); return; }
                    pop();
                }
                bool topLevel = frames.size() == 1;
                size_t resume = frame().returnPos;
                frames.pop_back();
                if (topLevel) {
                    result.status = KisakScriptExecStatus::Completed;
                    result.returnValue = std::move(ret);
                    halted = true;
                    return;
                }
                top() = std::move(ret);          // CodePos slot becomes the return value
                cursor.pos = resume;
                break;
            }
            case KisakScriptOpcode::OP_End: {
                while (top().type != KisakScriptValueType::CodePos) {
                    if (stack.empty()) { RuntimeError("OP_End stack underflow"); return; }
                    pop();
                }
                bool topLevel = frames.size() == 1;
                size_t resume = frame().returnPos;
                frames.pop_back();
                if (topLevel) {
                    result.status = KisakScriptExecStatus::Completed;
                    result.returnValue = KisakScriptValue::Undefined();
                    halted = true;
                    return;
                }
                top() = KisakScriptValue::Undefined();
                cursor.pos = resume;
                break;
            }
            case KisakScriptOpcode::OP_DecTop:
                if (stack.empty()) { RuntimeError("OP_DecTop underflow"); return; }
                pop();
                break;

            // ---- jumps ----
            case KisakScriptOpcode::OP_JumpOnFalse: {
                RunCastBool(top());
                if (halted) return;
                uint16_t off = cursor.ReadUnsignedShort();
                bool truthy = top().i != 0;
                pop();
                if (!truthy) cursor.pos += off;
                break;
            }
            case KisakScriptOpcode::OP_JumpOnTrue: {
                RunCastBool(top());
                if (halted) return;
                uint16_t off = cursor.ReadUnsignedShort();
                bool truthy = top().i != 0;
                pop();
                if (truthy) cursor.pos += off;
                break;
            }
            case KisakScriptOpcode::OP_JumpOnFalseExpr: {
                RunCastBool(top());
                if (halted) return;
                uint16_t off = cursor.ReadUnsignedShort();
                if (top().i != 0) { pop(); } else { cursor.pos += off; }
                break;
            }
            case KisakScriptOpcode::OP_JumpOnTrueExpr: {
                RunCastBool(top());
                if (halted) return;
                uint16_t off = cursor.ReadUnsignedShort();
                if (top().i == 0) { pop(); } else { cursor.pos += off; }
                break;
            }
            case KisakScriptOpcode::OP_jump: {
                int32_t off = cursor.ReadInt();
                cursor.pos = static_cast<size_t>(static_cast<int64_t>(cursor.pos) + off);
                break;
            }
            case KisakScriptOpcode::OP_jumpback: {
                uint16_t off = cursor.ReadUnsignedShort();
                cursor.pos -= off;
                break;
            }

            // ---- inc / dec (ref set by a preceding OP_EvalLocalVariableRefCached*) ----
            case KisakScriptOpcode::OP_inc:
            case KisakScriptOpcode::OP_dec: {
                if (!SlotValid(frame().refSlot)) { RuntimeError("++/-- without a ref"); return; }
                KisakScriptValue& slot = frame().locals[frame().refSlot];
                if (slot.type != KisakScriptValueType::Int) {
                    RuntimeError("++/-- must be applied to an int (applied to " + slot.Describe() + ")");
                    return;
                }
                if (opcode == KisakScriptOpcode::OP_inc) ++slot.i; else --slot.i;
                // Retail's OP_inc/OP_dec consume the trailing OP_SetVariableField
                // the compiler always emits after them (scr_vm.cpp:3114/3130).
                if (cursor.pos < program.bytecode.size() &&
                    static_cast<KisakScriptOpcode>(program.bytecode[cursor.pos]) ==
                        KisakScriptOpcode::OP_SetVariableField) {
                    cursor.pos += 1;
                }
                break;
            }

            // ---- arithmetic / comparison (binary: a=top-1, b=top, result in a) ----
            case KisakScriptOpcode::OP_plus:
            case KisakScriptOpcode::OP_minus:
            case KisakScriptOpcode::OP_multiply:
            case KisakScriptOpcode::OP_divide:
            case KisakScriptOpcode::OP_mod:
            case KisakScriptOpcode::OP_bit_or:
            case KisakScriptOpcode::OP_bit_ex_or:
            case KisakScriptOpcode::OP_bit_and:
            case KisakScriptOpcode::OP_shift_left:
            case KisakScriptOpcode::OP_shift_right:
            case KisakScriptOpcode::OP_equality:
            case KisakScriptOpcode::OP_inequality:
            case KisakScriptOpcode::OP_less:
            case KisakScriptOpcode::OP_greater:
            case KisakScriptOpcode::OP_less_equal:
            case KisakScriptOpcode::OP_greater_equal: {
                if (stack.size() < 2) { RuntimeError("binary op stack underflow"); return; }
                KisakScriptValue b = pop();
                KisakScriptValue& a = top();
                switch (opcode) {
                    case KisakScriptOpcode::OP_plus: RunPlus(a, b); break;
                    case KisakScriptOpcode::OP_minus: RunMinus(a, b); break;
                    case KisakScriptOpcode::OP_multiply: RunMultiply(a, b); break;
                    case KisakScriptOpcode::OP_divide: RunDivide(a, b); break;
                    case KisakScriptOpcode::OP_mod: RunMod(a, b); break;
                    case KisakScriptOpcode::OP_bit_or: RunBitwise(a, b, '|'); break;
                    case KisakScriptOpcode::OP_bit_ex_or: RunBitwise(a, b, '^'); break;
                    case KisakScriptOpcode::OP_bit_and: RunBitwise(a, b, '&'); break;
                    case KisakScriptOpcode::OP_shift_left: RunBitwise(a, b, '<'); break;
                    case KisakScriptOpcode::OP_shift_right: RunBitwise(a, b, '>'); break;
                    case KisakScriptOpcode::OP_equality: RunEquality(a, b, true); break;
                    case KisakScriptOpcode::OP_inequality: RunEquality(a, b, false); break;
                    case KisakScriptOpcode::OP_less: RunCompare(a, b, '<'); break;
                    case KisakScriptOpcode::OP_greater: RunCompare(a, b, '>'); break;
                    case KisakScriptOpcode::OP_less_equal: RunCompare(a, b, 'l'); break;
                    case KisakScriptOpcode::OP_greater_equal: RunCompare(a, b, 'g'); break;
                    default: break;
                }
                if (halted) return;
                break;
            }

            // ---- unary ----
            case KisakScriptOpcode::OP_CastBool:
                RunCastBool(top());
                if (halted) return;
                break;
            case KisakScriptOpcode::OP_BoolNot: {
                RunCastBool(top());
                if (halted) return;
                top().i = (top().i == 0) ? 1 : 0;
                break;
            }
            case KisakScriptOpcode::OP_BoolComplement: {
                if (top().type != KisakScriptValueType::Int) {
                    RuntimeError("~ cannot be applied to " + top().Describe());
                    return;
                }
                top().i = ~top().i;
                break;
            }

            case KisakScriptOpcode::OP_NOP:
                break;
            case KisakScriptOpcode::OP_abort:
                result.status = KisakScriptExecStatus::Aborted;
                result.stopPos = opPos;
                result.stopOpcode = opcode;
                halted = true;
                return;

            // Everything else (entity fields, OP_CallBuiltin*, waittill/notify/
            // endon/wait/switch/threading, ...) is out of step 3's subset. Log
            // exactly what was hit and where, then STOP — never silently no-op,
            // so step 4/8 get a precise signal about what is still missing.
            default: {
                std::string msg = "unsupported opcode " + DescribeScriptOpcode(opcode) +
                    " (0x" + [&] {
                        char b[8];
                        std::snprintf(b, sizeof(b), "%02X", static_cast<unsigned>(
                            static_cast<uint8_t>(opcode)));
                        return std::string(b);
                    }() + ") at bytecode offset " + std::to_string(opPos);
                (logFn ? logFn : DefaultLog)(msg);
                Fail(KisakScriptExecStatus::UnsupportedOpcode, msg);
                result.stopPos = opPos;
                result.stopOpcode = opcode;
                return;
            }
        }
    }
}

}  // namespace

KisakScriptExecResult KisakScriptVm::Execute(const KisakScriptProgram& program,
                                             uint32_t entryOffset) {
    Interpreter interp(program);
    interp.logFn = logFn;
    interp.maxSteps = maxSteps;

    // Base marker for the entry frame: the entry function's prologue
    // (OP_checkclearparams) converts this PreCodePos into the CodePos frame
    // boundary, exactly as a called function's prologue does. This keeps the
    // top-level thread and nested calls on one uniform mechanism.
    interp.push(KisakScriptValue::Marker(KisakScriptValueType::PreCodePos));
    Frame entry;
    entry.returnPos = 0;
    interp.frames.push_back(std::move(entry));
    interp.cursor.pos = entryOffset;

    interp.Run();
    return interp.result;
}
