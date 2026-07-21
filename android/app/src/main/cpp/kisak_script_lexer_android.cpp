#include "kisak_script_lexer_android.h"

#include <cctype>
#include <cstdlib>
#include <unordered_map>

namespace {

const std::unordered_map<std::string, KisakScriptKeyword>& KeywordTable() {
    static const std::unordered_map<std::string, KisakScriptKeyword> table = {
        {"if", KisakScriptKeyword::If},
        {"else", KisakScriptKeyword::Else},
        {"while", KisakScriptKeyword::While},
        {"for", KisakScriptKeyword::For},
        {"switch", KisakScriptKeyword::Switch},
        {"case", KisakScriptKeyword::Case},
        {"default", KisakScriptKeyword::Default},
        {"break", KisakScriptKeyword::Break},
        {"continue", KisakScriptKeyword::Continue},
        {"return", KisakScriptKeyword::Return},
        {"thread", KisakScriptKeyword::Thread},
        {"wait", KisakScriptKeyword::Wait},
        {"waittill", KisakScriptKeyword::Waittill},
        {"waittillmatch", KisakScriptKeyword::Waittillmatch},
        {"waittillframeend", KisakScriptKeyword::Waittillframeend},
        {"notify", KisakScriptKeyword::Notify},
        {"endon", KisakScriptKeyword::Endon},
        {"self", KisakScriptKeyword::Self},
        {"level", KisakScriptKeyword::Level},
        {"game", KisakScriptKeyword::Game},
        {"anim", KisakScriptKeyword::Anim},
        {"animtree", KisakScriptKeyword::Animtree},
        {"usingtree", KisakScriptKeyword::Usingtree},
        {"true", KisakScriptKeyword::KwTrue},
        {"false", KisakScriptKeyword::KwFalse},
        {"undefined", KisakScriptKeyword::Undefined},
        {"vector", KisakScriptKeyword::Vector},
        {"breakpoint", KisakScriptKeyword::Breakpoint},
        {"prof_begin", KisakScriptKeyword::ProfBegin},
        {"prof_end", KisakScriptKeyword::ProfEnd},
        {"include", KisakScriptKeyword::Include},
    };
    return table;
}

bool IsIdentStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool IsIdentCont(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool IsDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

// Ports the escape subset actually needed by real .gsc string content
// (StringValue, scr_yacc2.cpp ~line 75+, handles \n and \r at minimum —
// this lexer covers the common C-string escape set rather than
// transliterating that function byte-for-byte, since step 6 is a
// hand-written-against-understanding port, not a transliteration).
void AppendDecodedChar(std::string& out, char c) {
    switch (c) {
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        case 'r': out += '\r'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        default: out += c; break;  // unknown escape: pass the char through literally
    }
}

class Lexer {
public:
    explicit Lexer(const std::string& src) : src_(src) {}

    KisakScriptLexResult Run() {
        while (!AtEnd()) {
            SkipWhitespaceAndComments();
            if (AtEnd()) break;
            const uint32_t startLine = line_;
            const char c = Peek();

            if (IsIdentStart(c)) {
                LexIdentifierOrKeyword(startLine);
            } else if (IsDigit(c) || (c == '.' && pos_ + 1 < src_.size() && IsDigit(src_[pos_ + 1]))) {
                LexNumber(startLine);
            } else if (c == '"') {
                LexString(startLine, /*isInterned=*/false);
            } else if (c == '&' && Peek(1) == '"') {
                Advance();  // consume '&', leaving the string starting at '"'
                LexString(startLine, /*isInterned=*/true);
            } else {
                LexOperatorOrError(startLine);
            }
        }
        return std::move(result_);
    }

private:
    const std::string& src_;
    size_t pos_ = 0;
    uint32_t line_ = 1;
    KisakScriptLexResult result_;

    bool AtEnd() const { return pos_ >= src_.size(); }
    char Peek(size_t ahead = 0) const {
        return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
    }
    char Advance() {
        char c = src_[pos_++];
        if (c == '\n') ++line_;
        return c;
    }

    void Error(uint32_t atLine, const std::string& message) {
        result_.errors.push_back("line " + std::to_string(atLine) + ": " + message);
    }

    void SkipWhitespaceAndComments() {
        while (!AtEnd()) {
            char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                Advance();
            } else if (c == '/' && Peek(1) == '/') {
                while (!AtEnd() && Peek() != '\n') Advance();
            } else if (c == '/' && Peek(1) == '*') {
                const uint32_t startLine = line_;
                Advance(); Advance();
                bool closed = false;
                while (!AtEnd()) {
                    if (Peek() == '*' && Peek(1) == '/') { Advance(); Advance(); closed = true; break; }
                    Advance();
                }
                if (!closed) Error(startLine, "unterminated block comment");
            } else {
                break;
            }
        }
    }

    void LexIdentifierOrKeyword(uint32_t startLine) {
        size_t start = pos_;
        while (!AtEnd() && IsIdentCont(Peek())) Advance();
        std::string text = src_.substr(start, pos_ - start);

        KisakScriptToken tok;
        tok.text = text;
        tok.line = startLine;
        const auto& keywords = KeywordTable();
        auto it = keywords.find(text);
        if (it != keywords.end()) {
            tok.type = KisakScriptTokenType::Keyword;
            tok.keyword = it->second;
        } else {
            tok.type = KisakScriptTokenType::Identifier;
        }
        result_.tokens.push_back(std::move(tok));
    }

    // Integers and floats, including leading-dot floats (`.1`, `.5` — real
    // syntax, e.g. killhouse.gsc's `wait .1;`). Unary minus is NOT consumed
    // here: the reference grammar treats negation as a separate AST node
    // (ENUM_minus_integer/ENUM_minus_float, scr_yacc.h) built from a plain
    // MINUS operator token plus a literal, not a signed-literal lexer rule.
    void LexNumber(uint32_t startLine) {
        size_t start = pos_;
        bool isFloat = false;
        if (Peek() == '.') {
            isFloat = true;
            Advance();
            while (!AtEnd() && IsDigit(Peek())) Advance();
        } else {
            while (!AtEnd() && IsDigit(Peek())) Advance();
            if (Peek() == '.' && IsDigit(Peek(1))) {
                isFloat = true;
                Advance();
                while (!AtEnd() && IsDigit(Peek())) Advance();
            } else if (Peek() == '.' && !IsIdentStart(Peek(1)) && !IsDigit(Peek(1))) {
                // e.g. "5." with nothing meaningful after the dot in this
                // corpus's observed style — treat as float with an empty
                // fractional part rather than splitting into INT then DOT,
                // matching ordinary C-family lexer behavior.
                isFloat = true;
                Advance();
            }
        }
        std::string text = src_.substr(start, pos_ - start);

        KisakScriptToken tok;
        tok.text = text;
        tok.line = startLine;
        if (isFloat) {
            tok.type = KisakScriptTokenType::FloatLiteral;
            tok.floatValue = std::strtof(text.c_str(), nullptr);
        } else {
            tok.type = KisakScriptTokenType::IntLiteral;
            tok.intValue = std::atoi(text.c_str());
        }
        result_.tokens.push_back(std::move(tok));
    }

    // `isInterned` distinguishes `&"..."` (IStringLiteral) from `"..."`
    // (StringLiteral) — see the header comment for why this is ONE token
    // either way, not `&` + STRING. Called with pos_ already at the
    // opening `"` in both cases (the caller consumes the leading `&`).
    void LexString(uint32_t startLine, bool isInterned) {
        Advance();  // opening quote
        std::string decoded;
        bool closed = false;
        while (!AtEnd()) {
            char c = Peek();
            if (c == '"') { Advance(); closed = true; break; }
            if (c == '\n') break;  // unterminated on this line — real .gsc strings don't span lines
            if (c == '\\' && pos_ + 1 < src_.size()) {
                Advance();
                AppendDecodedChar(decoded, Advance());
            } else {
                decoded += Advance();
            }
        }
        if (!closed) Error(startLine, "unterminated string literal");

        KisakScriptToken tok;
        tok.type = isInterned ? KisakScriptTokenType::IStringLiteral
                               : KisakScriptTokenType::StringLiteral;
        tok.stringValue = decoded;
        tok.text = decoded;
        tok.line = startLine;
        result_.tokens.push_back(std::move(tok));
    }

    // Longest-match-first over the operator set confirmed (or, for a few
    // symmetric cases, reasonably inferred) from real .gsc text — see the
    // header comment for exactly which 2-char operators were grep-verified
    // vs included by pattern. `\` is its own single-char operator (path
    // separator in namespaced calls/includes, e.g. `maps\_utility`), not
    // an escape — GSC's `\` only means "escape" inside a string literal,
    // handled by LexString above, not at top level.
    void LexOperatorOrError(uint32_t startLine) {
        static const char* kTwoChar[] = {
            "::", "==", "!=", "<=", ">=", "&&", "||", "++", "--",
            "<<", ">>", "+=", "-=", "*=", "/=", "%=",
        };
        for (const char* op : kTwoChar) {
            if (Peek() == op[0] && Peek(1) == op[1]) {
                Advance(); Advance();
                KisakScriptToken tok;
                tok.type = KisakScriptTokenType::Operator;
                tok.text = op;
                tok.line = startLine;
                result_.tokens.push_back(std::move(tok));
                return;
            }
        }
        static const std::string kOneChar = "+-*/%=<>!~&|^(){}[];,.\\#:?";
        char c = Peek();
        if (kOneChar.find(c) != std::string::npos) {
            Advance();
            KisakScriptToken tok;
            tok.type = KisakScriptTokenType::Operator;
            tok.text = std::string(1, c);
            tok.line = startLine;
            result_.tokens.push_back(std::move(tok));
            return;
        }
        Error(startLine, std::string("unrecognized character '") + c + "'");
        Advance();
    }
};

}  // namespace

KisakScriptLexResult TokenizeGscSource(const std::string& source) {
    Lexer lexer(source);
    return lexer.Run();
}

std::string DescribeTokenType(KisakScriptTokenType type) {
    switch (type) {
        case KisakScriptTokenType::EndOfFile: return "EndOfFile";
        case KisakScriptTokenType::Identifier: return "Identifier";
        case KisakScriptTokenType::Keyword: return "Keyword";
        case KisakScriptTokenType::IntLiteral: return "IntLiteral";
        case KisakScriptTokenType::FloatLiteral: return "FloatLiteral";
        case KisakScriptTokenType::StringLiteral: return "StringLiteral";
        case KisakScriptTokenType::IStringLiteral: return "IStringLiteral";
        case KisakScriptTokenType::Operator: return "Operator";
        case KisakScriptTokenType::Error: return "Error";
    }
    return "?";
}

std::string DescribeKeyword(KisakScriptKeyword keyword) {
    switch (keyword) {
        case KisakScriptKeyword::None: return "-";
        case KisakScriptKeyword::If: return "if";
        case KisakScriptKeyword::Else: return "else";
        case KisakScriptKeyword::While: return "while";
        case KisakScriptKeyword::For: return "for";
        case KisakScriptKeyword::Switch: return "switch";
        case KisakScriptKeyword::Case: return "case";
        case KisakScriptKeyword::Default: return "default";
        case KisakScriptKeyword::Break: return "break";
        case KisakScriptKeyword::Continue: return "continue";
        case KisakScriptKeyword::Return: return "return";
        case KisakScriptKeyword::Thread: return "thread";
        case KisakScriptKeyword::Wait: return "wait";
        case KisakScriptKeyword::Waittill: return "waittill";
        case KisakScriptKeyword::Waittillmatch: return "waittillmatch";
        case KisakScriptKeyword::Waittillframeend: return "waittillframeend";
        case KisakScriptKeyword::Notify: return "notify";
        case KisakScriptKeyword::Endon: return "endon";
        case KisakScriptKeyword::Self: return "self";
        case KisakScriptKeyword::Level: return "level";
        case KisakScriptKeyword::Game: return "game";
        case KisakScriptKeyword::Anim: return "anim";
        case KisakScriptKeyword::Animtree: return "animtree";
        case KisakScriptKeyword::Usingtree: return "usingtree";
        case KisakScriptKeyword::KwTrue: return "true";
        case KisakScriptKeyword::KwFalse: return "false";
        case KisakScriptKeyword::Undefined: return "undefined";
        case KisakScriptKeyword::Vector: return "vector";
        case KisakScriptKeyword::Breakpoint: return "breakpoint";
        case KisakScriptKeyword::ProfBegin: return "prof_begin";
        case KisakScriptKeyword::ProfEnd: return "prof_end";
        case KisakScriptKeyword::Include: return "include";
    }
    return "?";
}

std::string DescribeToken(const KisakScriptToken& token) {
    std::string out = "[" + std::to_string(token.line) + "] " + DescribeTokenType(token.type);
    switch (token.type) {
        case KisakScriptTokenType::Keyword:
            out += " " + DescribeKeyword(token.keyword);
            break;
        case KisakScriptTokenType::IntLiteral:
            out += " " + std::to_string(token.intValue);
            break;
        case KisakScriptTokenType::FloatLiteral:
            out += " " + std::to_string(token.floatValue);
            break;
        case KisakScriptTokenType::StringLiteral:
        case KisakScriptTokenType::IStringLiteral:
            out += " \"" + token.stringValue + "\"";
            break;
        case KisakScriptTokenType::Identifier:
        case KisakScriptTokenType::Operator:
        case KisakScriptTokenType::Error:
            out += " '" + token.text + "'";
            break;
        case KisakScriptTokenType::EndOfFile:
            break;
    }
    return out;
}
