#pragma once

#include <cstdint>
#include <string>
#include <vector>

// GScript lexer: real source text -> a token stream, ready for step 7's
// parser to consume. No AST, no compilation — blueprint
// plans/android-gscript-vm-port.md, step 6.
//
// Lexer location — file correction (already made by step 6's own context
// brief, restated here since it's easy to rediscover the wrong file):
// src/script/scr_parser.cpp has NO lexer — it's source-position/line-number
// bookkeeping only. The real lexer is `yylex` in src/script/scr_yacc2.cpp
// (2,119 lines, flex-generated). Since that file is machine-generated DFA
// tables (yy_accept/yy_ec/yy_base/yy_nxt/...) with no human-readable
// keyword strings anywhere in the generated dispatch — the accepting
// actions are just `return 261;`, `return 262;`, etc, with the actual
// keyword text fully absorbed into the compressed DFA — this is NOT
// transliterated line-by-line. Instead: the keyword set is cross-referenced
// from scr_yacc.h's Enum_t (src/script/scr_yacc.h:7-97), which encodes each
// GSC keyword as its own grammar node kind (ENUM_if, ENUM_while,
// ENUM_waittill, ENUM_thread, ...) since this grammar's keywords are also
// AST leaf types; operators/literal formats/comment style are cross-checked
// directly against real .gsc text (plans/gscript-real-source-notes.md +
// the fuller Step 1 corpus) rather than assumed from generic C-family
// lexer conventions.
//
// One genuinely non-obvious finding from reading scr_yacc2.cpp's accepting
// actions directly (case 7/8 in the flex switch, ~line 503-511): `"..."`
// and `&"..."` are lexed via the SAME StringValue() escape-decoder, but
// `&"..."` strips a 2-char prefix / 1-char suffix (`yytext+2, yyleng-3`)
// vs plain strings' 1-char prefix/suffix (`yytext+1, yyleng-2`) — i.e.
// `&"KEY"` is ONE lexical token (an interned/localized string literal,
// matching OP_GetIString in the VM opcode set), not the `&` operator
// followed by a separate STRING token. This lexer preserves that: IString
// is its own token type, not decomposed.
//
// Known real syntax this lexer's design is grounded in (grep-verified
// against the Step 1 `.gsc` corpus, not assumed): `//` line comments,
// `/* ... */` block comments (found in aitype/*.gsc QUAKED spawner
// doc-comments), `::` (namespaced/function-pointer calls), `++`/`--`,
// `&&`, `!=`, `==`, `-=` (compound assignment — confirmed used at least
// once; `+=`/`*=`/`/=`/`%=` are included by the same pattern but were NOT
// directly observed in this corpus), leading-dot float literals (`.1`,
// `.5` — killhouse.gsc's `wait .1;` etc, no leading `0`), `\` as a path
// separator in namespaced calls/includes (`maps\_utility`), `#include`.
// NOT observed in this corpus (included or omitted per the note at each
// point below, not silently assumed): hex integer literals, ternary `?:`,
// bit-shift operators `<<`/`>>`, float literals with an `f` suffix.

enum class KisakScriptTokenType : uint8_t {
    EndOfFile,
    Identifier,
    Keyword,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    IStringLiteral,  // &"..." — interned/localized string (OP_GetIString)
    Operator,        // punctuation/operators; exact text distinguishes which
    Error,           // unrecognized character — lexing continues past it
};

// Keywords cross-referenced from scr_yacc.h's Enum_t (see header comment).
// `usingtree`/`animtree`/`breakpoint`/`prof_begin`/`prof_end` were NOT seen
// in the Step 1 corpus but are real Enum_t-backed keywords, kept here so
// the lexer at least classifies them correctly if step 9+ hits a script
// that uses them (deferred to the parser either way, per step 6's own
// "no AST yet" scope).
enum class KisakScriptKeyword : uint8_t {
    None = 0,
    If, Else, While, For, Switch, Case, Default, Break, Continue, Return,
    Thread, Waittill, Waittillmatch, Waittillframeend, Notify, Endon,
    Self, Level, Game, Anim, Animtree, Usingtree, KwTrue, KwFalse, Undefined,
    Vector, Breakpoint, ProfBegin, ProfEnd, Include,
};

struct KisakScriptToken {
    KisakScriptTokenType type = KisakScriptTokenType::EndOfFile;
    std::string text;  // raw source lexeme (identifier/keyword/operator text,
                        // or the literal's source text before decoding)
    KisakScriptKeyword keyword = KisakScriptKeyword::None;  // valid when type == Keyword
    int32_t intValue = 0;         // valid when type == IntLiteral
    float floatValue = 0.0f;      // valid when type == FloatLiteral
    std::string stringValue;      // valid when type == StringLiteral/IStringLiteral (escapes decoded)
    uint32_t line = 1;             // 1-based source line the token starts on
};

struct KisakScriptLexResult {
    std::vector<KisakScriptToken> tokens;  // does NOT include an EndOfFile sentinel
    std::vector<std::string> errors;       // "line N: message" — empty means clean
};

KisakScriptLexResult TokenizeGscSource(const std::string& source);

std::string DescribeToken(const KisakScriptToken& token);
std::string DescribeTokenType(KisakScriptTokenType type);
std::string DescribeKeyword(KisakScriptKeyword keyword);
