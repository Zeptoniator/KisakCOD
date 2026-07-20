#include "kisak_script_vm_android.h"

#include <cstring>

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
