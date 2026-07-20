#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "kisak_script_lexer_android.h"

// GScript parser: step 6's real token stream -> an AST, for a deliberately
// restricted grammar subset — blueprint plans/android-gscript-vm-port.md,
// step 7. No compilation yet (step 8).
//
// Grammar source: scr_yacc.cpp's yyparse (7,993 lines, bison-generated —
// scr_yacc2.cpp is the LEXER, not the grammar, see step 6's file-correction
// note). AST node shapes cross-referenced from scr_parsetree.cpp/.h: retail
// represents every AST node as a generic tagged N-ary record — `sval_u
// node[]` where node[0].type is an Enum_t tag and node[1..8] are child
// sval_u slots, bump-allocated via Hunk_UserAlloc (node0..node8 helpers,
// scr_parsetree.h:9-54). This is a deliberate representational departure,
// not a literal port: a generic untyped 8-slot node is exactly the kind of
// thing that is painful and error-prone to write and review in idiomatic
// C++, and a kind-tagged struct with named/positional children via
// std::unique_ptr is equivalent in expressive power. Node KINDS still
// track Enum_t's naming (scr_yacc.h:7-97) where a kind exists there
// (If/IfElse/While/For/Return/Call/...); this parser's subset only
// produces a fraction of Enum_t's full set.
//
// Approach — hand-written recursive descent, NOT a transliteration of
// yyparse: bison-generated yyparse is an LALR table-driven automaton
// (~8,000 lines of generated shift/reduce tables), and yacc/bison isn't
// part of this Android build's toolchain. A hand-written recursive-descent
// parser producing the same AST shape is the practical substitute the
// plan itself explicitly allows for this exact reason.
//
// Deliberately restricted grammar (explicitly deferred, matching the plan's
// own scope cut): switch/case, waittill/notify/endon/wait/threading,
// arrays/structs, namespaced calls (path\file::func()), function pointers
// (::func), and any COD-specific syntax sugar beyond what's needed for
// this step's target script. This subset's goal is the smallest grammar
// that parses the SIMPLEST real script identified in step 1
// (character/character_sp_sas_ct_neal.gsc — a 2-function, 6-line script
// with no control flow at all), NOT full language coverage — but it also
// covers the broader baseline named in this step's own context brief
// (assignment, if/else, while/for, function calls, return, arithmetic/
// comparison/logical expressions) so step 8's compiler has a subset that
// actually matches what step 3/4's VM+builtins can already execute.
//
// Real GSC method-call syntax (grep-confirmed pervasively in the corpus,
// e.g. `self setModel(...)`, `level.player setOrigin(...)`,
// `door playsound(...)`): <object-expr> <bareword> ( args ) — NO dot
// before the method name (dot is reserved for FIELD access/assignment,
// e.g. `self.voice = "...";`). This parser treats that as its own
// production (MethodCall), disambiguated by one-token lookahead after a
// postfix (primary + `.field` chain) expression.

enum class KisakAstNodeKind : uint8_t {
    Program,               // children = top-level FunctionDef nodes (#include directives are consumed and dropped, not represented)
    FunctionDef,           // text = function name; stringList = param names; children[0] = Block
    Block,                 // children = statements
    IfStatement,           // children[0] = condition, children[1] = then-branch, children[2] = else-branch (optional)
    WhileStatement,        // children[0] = condition, children[1] = body
    ForStatement,          // children[0] = init (optional), children[1] = condition (optional), children[2] = increment (optional), children[3] = body
    ReturnStatement,       // children[0] = expr (optional)
    ExpressionStatement,   // children[0] = expr
    Assignment,            // children[0] = target (Identifier or FieldAccess), children[1] = value; text = compound-assign operator ("", "+=", "-=", "*=", "/=", "%=")
    BinaryExpr,            // text = operator; children[0] = left, children[1] = right
    UnaryExpr,             // text = operator; children[0] = operand
    CallExpr,              // text = function name; children = args
    MethodCallExpr,        // text = method name; children[0] = object, children[1..] = args
    FieldAccessExpr,       // text = field name; children[0] = object
    IdentifierExpr,        // text = name (also used for self/level/game keyword references)
    IntLiteralExpr,        // intValue
    FloatLiteralExpr,      // floatValue
    StringLiteralExpr,     // text = decoded string
    IStringLiteralExpr,    // text = decoded string (interned/localized ref)
    BoolLiteralExpr,       // intValue = 0 or 1
    UndefinedLiteralExpr,
};

struct KisakAstNode {
    KisakAstNodeKind kind;
    uint32_t line = 0;
    std::string text;      // meaning depends on kind — see KisakAstNodeKind comments
    int32_t intValue = 0;
    float floatValue = 0.0f;
    std::vector<std::string> stringList;  // FunctionDef's parameter names
    std::vector<std::unique_ptr<KisakAstNode>> children;

    explicit KisakAstNode(KisakAstNodeKind k, uint32_t ln) : kind(k), line(ln) {}
};

struct KisakScriptParseResult {
    std::unique_ptr<KisakAstNode> program;  // null if parsing failed
    std::vector<std::string> errors;        // empty means clean parse
};

KisakScriptParseResult ParseGscTokens(const std::vector<KisakScriptToken>& tokens);

// Convenience: tokenize then parse in one call (host test harness / step 9
// wiring both want this).
KisakScriptParseResult ParseGscSource(const std::string& source);

std::string DescribeAstNodeKind(KisakAstNodeKind kind);

// Multi-line, indented debug dump of the whole tree — the exit criteria's
// "dump the AST structure ... for manual review" deliverable.
std::string DumpAst(const KisakAstNode& node, int indent = 0);
