#include "kisak_script_parser_android.h"

#include <cstdio>

namespace {

using Kind = KisakAstNodeKind;

std::unique_ptr<KisakAstNode> MakeNode(Kind kind, uint32_t line) {
    return std::make_unique<KisakAstNode>(kind, line);
}

bool IsObjectKeyword(KisakScriptKeyword kw) {
    // self/level/game/anim are addressable object references in expression
    // position (e.g. `self.voice`, `level.player`), distinct from pure
    // value keywords (true/false/undefined) which can never be a field-
    // access or method-call target.
    return kw == KisakScriptKeyword::Self || kw == KisakScriptKeyword::Level ||
           kw == KisakScriptKeyword::Game || kw == KisakScriptKeyword::Anim;
}

// Entity/object-model blueprint step 3, Task 3: waittill/notify/endon (and
// their variants) are lexer KEYWORDS, not identifiers -- neither the
// pre-existing MethodCallExpr production nor this step's new
// MethodThreadCallStatement production can ever accept one as a call-target
// (both require an Identifier-typed token), so `level notify(...)` etc.
// already fail to parse today regardless of this helper. This is message-
// quality polish only (a specific "deferred subsystem" error instead of a
// generic "expected ';'"), NOT a safety mechanism preventing a miscompile
// that adversarial review confirmed cannot occur.
bool IsDeferredSubsystemKeyword(KisakScriptKeyword kw) {
    return kw == KisakScriptKeyword::Waittill || kw == KisakScriptKeyword::Waittillmatch ||
           kw == KisakScriptKeyword::Waittillframeend || kw == KisakScriptKeyword::Notify ||
           kw == KisakScriptKeyword::Endon;
}

class Parser {
public:
    explicit Parser(const std::vector<KisakScriptToken>& tokens) : tokens_(tokens) {}

    KisakScriptParseResult Run() {
        KisakScriptParseResult result;
        result.program = ParseProgram();
        if (failed_) result.program.reset();
        result.errors = errors_;
        return result;
    }

private:
    const std::vector<KisakScriptToken>& tokens_;
    size_t pos_ = 0;
    bool failed_ = false;
    std::vector<std::string> errors_;

    bool AtEnd() const { return pos_ >= tokens_.size(); }
    const KisakScriptToken& Peek(size_t ahead = 0) const {
        static const KisakScriptToken kEof{};
        size_t i = pos_ + ahead;
        return i < tokens_.size() ? tokens_[i] : kEof;
    }
    uint32_t CurLine() const { return AtEnd() ? (tokens_.empty() ? 1 : tokens_.back().line) : Peek().line; }

    const KisakScriptToken& Advance() {
        static const KisakScriptToken kEof{};
        if (AtEnd()) return kEof;
        return tokens_[pos_++];
    }

    bool CheckOp(const char* op) const {
        return !AtEnd() && Peek().type == KisakScriptTokenType::Operator && Peek().text == op;
    }
    bool CheckKeyword(KisakScriptKeyword kw) const {
        return !AtEnd() && Peek().type == KisakScriptTokenType::Keyword && Peek().keyword == kw;
    }
    bool MatchOp(const char* op) {
        if (CheckOp(op)) { Advance(); return true; }
        return false;
    }
    bool MatchKeyword(KisakScriptKeyword kw) {
        if (CheckKeyword(kw)) { Advance(); return true; }
        return false;
    }

    // Records an error, marks the parse as failed, and stops further
    // production (callers check failed_ and unwind) — matches this
    // subsystem's established stop-on-first-error philosophy (steps 3/4's
    // KisakScriptExecResult), simpler than panic-mode recovery for a step
    // whose exit criteria is "the target script parses cleanly," not
    // error-recovery robustness.
    void Fail(const std::string& message) {
        if (failed_) return;
        failed_ = true;
        errors_.push_back("line " + std::to_string(CurLine()) + ": " + message);
    }

    bool ExpectOp(const char* op) {
        if (MatchOp(op)) return true;
        Fail(std::string("expected '") + op + "', got " +
             (AtEnd() ? "end of input" : "'" + Peek().text + "'"));
        return false;
    }
    bool ExpectIdentifier(std::string& out) {
        if (!AtEnd() && Peek().type == KisakScriptTokenType::Identifier) {
            out = Advance().text;
            return true;
        }
        Fail("expected identifier, got " + (AtEnd() ? "end of input" : "'" + Peek().text + "'"));
        return false;
    }

    // ---- top level ----

    std::unique_ptr<KisakAstNode> ParseProgram() {
        auto program = MakeNode(Kind::Program, 1);
        while (!AtEnd() && !failed_) {
            if (CheckOp("#")) {
                SkipIncludeDirective();
                continue;
            }
            auto fn = ParseFunctionDef();
            if (failed_) return program;
            program->children.push_back(std::move(fn));
        }
        return program;
    }

    // `#include path\segment\more;` — consumed and dropped (Program's own
    // comment documents this): step 7's target script has no #include
    // lines at all, and giving this directive real AST content would be
    // scope creep beyond what any real target here needs. Still parsed
    // (not just skipped-to-semicolon blindly) so a malformed directive is
    // still a real parse error, not silently swallowed.
    void SkipIncludeDirective() {
        uint32_t line = CurLine();
        ExpectOp("#");
        if (failed_) return;
        if (!MatchKeyword(KisakScriptKeyword::Include)) {
            Fail("expected 'include' after '#'");
            return;
        }
        std::string ident;
        if (!ExpectIdentifier(ident)) return;
        while (MatchOp("\\")) {
            if (!ExpectIdentifier(ident)) return;
        }
        (void)line;
        ExpectOp(";");
    }

    std::unique_ptr<KisakAstNode> ParseFunctionDef() {
        uint32_t line = CurLine();
        std::string name;
        if (!ExpectIdentifier(name)) return nullptr;
        auto fn = MakeNode(Kind::FunctionDef, line);
        fn->text = name;
        if (!ExpectOp("(")) return fn;
        if (!CheckOp(")")) {
            std::string param;
            if (!ExpectIdentifier(param)) return fn;
            fn->stringList.push_back(param);
            while (MatchOp(",")) {
                if (!ExpectIdentifier(param)) return fn;
                fn->stringList.push_back(param);
            }
        }
        if (!ExpectOp(")")) return fn;
        auto body = ParseBlock();
        if (body) fn->children.push_back(std::move(body));
        return fn;
    }

    // ---- statements ----

    std::unique_ptr<KisakAstNode> ParseBlock() {
        uint32_t line = CurLine();
        if (!ExpectOp("{")) return nullptr;
        auto block = MakeNode(Kind::Block, line);
        while (!failed_ && !AtEnd() && !CheckOp("}")) {
            auto stmt = ParseStatement();
            if (failed_) return block;
            if (stmt) block->children.push_back(std::move(stmt));
        }
        ExpectOp("}");
        return block;
    }

    std::unique_ptr<KisakAstNode> ParseStatement() {
        if (CheckOp("{")) return ParseBlock();
        if (CheckKeyword(KisakScriptKeyword::If)) return ParseIf();
        if (CheckKeyword(KisakScriptKeyword::While)) return ParseWhile();
        if (CheckKeyword(KisakScriptKeyword::For)) return ParseFor();
        if (CheckKeyword(KisakScriptKeyword::Return)) return ParseReturn();
        // Threading blueprint step 2: `thread`/`wait` are only recognized as
        // these dedicated statement kinds when they are the VERY FIRST token
        // of the statement — this is what naturally excludes object-prefixed
        // `<expr> thread ...` (an expression precedes `thread` there, so this
        // branch is never reached; it falls through to the ordinary
        // expression-statement path below and fails there instead, with a
        // clear "expected ';'"-style error, not a crash or silent misparse).
        if (CheckKeyword(KisakScriptKeyword::Thread)) return ParseThreadCall();
        if (CheckKeyword(KisakScriptKeyword::Wait)) return ParseWait();
        // Switch/loop-control blueprint (plans/android-gscript-switch-
        // control-flow.md) step 1. `break`/`continue` are trivial no-
        // expression statements (mirroring ParseReturn's own shape) --
        // whether they're actually inside a loop/switch is a COMPILER-level
        // check (Architecture fact 4), not a parser-level one, exactly like
        // every other semantic (non-grammatical) rejection in this port.
        if (CheckKeyword(KisakScriptKeyword::Switch)) return ParseSwitch();
        if (CheckKeyword(KisakScriptKeyword::Break)) {
            uint32_t line = CurLine();
            Advance();
            auto node = MakeNode(Kind::BreakStatement, line);
            ExpectOp(";");
            return node;
        }
        if (CheckKeyword(KisakScriptKeyword::Continue)) {
            uint32_t line = CurLine();
            Advance();
            auto node = MakeNode(Kind::ContinueStatement, line);
            ExpectOp(";");
            return node;
        }
        return ParseExpressionOrAssignmentStatement();
    }

    // Switch/loop-control blueprint step 1: `switch (subject) { case V: ...;
    // default: ...; }`. Real corpus confirms BOTH string- and integer-cased
    // switches (cargoship_extract.gsc:189, ally_sas_woodland_smg_mp5.gsc:26),
    // an optional `default:` clause (cargoship_extract.gsc:715, always last
    // in the real corpus but the grammar doesn't assume that), and case
    // bodies with or without a brace block (cargoship_extract.gsc:1765/249).
    std::unique_ptr<KisakAstNode> ParseSwitch() {
        uint32_t line = CurLine();
        Advance();  // 'switch'
        auto node = MakeNode(Kind::SwitchStatement, line);
        if (!ExpectOp("(")) return node;
        auto subject = ParseExpression();
        if (subject) node->children.push_back(std::move(subject));
        if (!ExpectOp(")")) return node;
        if (!ExpectOp("{")) return node;
        while (!failed_ && !AtEnd() && !CheckOp("}")) {
            auto clause = ParseCaseClause();
            if (failed_) return node;
            if (clause) node->children.push_back(std::move(clause));
        }
        ExpectOp("}");
        return node;
    }

    // A single `case <literal>:` or `default:` clause header, followed by a
    // flat run of ordinary statements (via the SAME ParseStatement() loop
    // ParseBlock itself uses) until the next `case`/`default`/`}` token.
    // children[0] (non-default only) = the case's literal value node
    // (IntLiteralExpr/StringLiteralExpr, reusing those existing kinds
    // rather than a duplicate type tag); the rest = the statement list.
    std::unique_ptr<KisakAstNode> ParseCaseClause() {
        uint32_t line = CurLine();
        auto clause = MakeNode(Kind::CaseClause, line);
        if (MatchKeyword(KisakScriptKeyword::Default)) {
            clause->isDefault = true;
            if (!ExpectOp(":")) return clause;
        } else if (MatchKeyword(KisakScriptKeyword::Case)) {
            auto value = ParseCaseValue();
            if (failed_) return clause;
            clause->children.push_back(std::move(value));
            if (!ExpectOp(":")) return clause;
        } else {
            Fail("expected 'case' or 'default' inside switch body, got " +
                 (AtEnd() ? "end of input" : "'" + Peek().text + "'"));
            return clause;
        }
        while (!failed_ && !AtEnd() && !CheckKeyword(KisakScriptKeyword::Case) &&
               !CheckKeyword(KisakScriptKeyword::Default) && !CheckOp("}")) {
            if (CheckOp("{")) {
                // Splice a brace block's statements FLAT into the clause
                // (matching real corpus shape, e.g. cargoship_extract.gsc:
                // 1765's `case "hallways": { ... }`) -- keeps CaseClause's
                // children a flat statement list (Architecture fact 1), not
                // wrapped in an extra Block node, even though EmitStatement
                // would actually handle a nested Block correctly too (this
                // port's locals are function-scoped, not block-scoped) --
                // flattening here is purely for a cleaner, more direct AST,
                // not a correctness requirement.
                auto block = ParseBlock();
                if (failed_) return clause;
                if (block) {
                    for (auto& child : block->children) {
                        clause->children.push_back(std::move(child));
                    }
                }
                continue;
            }
            auto stmt = ParseStatement();
            if (failed_) return clause;
            if (stmt) clause->children.push_back(std::move(stmt));
        }
        return clause;
    }

    // A case label: a plain int or string literal ONLY (matching every
    // real corpus example -- see the plan's own scope cut). Anything else
    // (a computed expression, an identifier, ...) is a specific, clear
    // parse error, not silently accepted.
    std::unique_ptr<KisakAstNode> ParseCaseValue() {
        uint32_t line = CurLine();
        if (!AtEnd() && Peek().type == KisakScriptTokenType::IntLiteral) {
            auto node = MakeNode(Kind::IntLiteralExpr, line);
            node->intValue = Advance().intValue;
            return node;
        }
        if (!AtEnd() && Peek().type == KisakScriptTokenType::StringLiteral) {
            auto node = MakeNode(Kind::StringLiteralExpr, line);
            node->text = Advance().stringValue;
            return node;
        }
        Fail("case label must be a plain int or string literal, got " +
             (AtEnd() ? "end of input" : "'" + Peek().text + "'"));
        return MakeNode(Kind::UndefinedLiteralExpr, line);
    }

    // Threading blueprint step 2: `thread <call-expr>;`. `thread` is purely
    // a statement-level prefix wrapping an EXISTING call-expression
    // production (bare `CallExpr` or namespaced `NamespacedCallExpr`) — no
    // new call-parsing logic, ParsePrimary already handles both forms
    // identically to how they parse without `thread` in front.
    std::unique_ptr<KisakAstNode> ParseThreadCall() {
        uint32_t line = CurLine();
        Advance();  // 'thread'
        auto node = MakeNode(Kind::ThreadCallStatement, line);
        auto callExpr = ParsePrimary();
        if (failed_) return node;
        if (callExpr->kind != Kind::CallExpr && callExpr->kind != Kind::NamespacedCallExpr) {
            Fail("'thread' must be followed by a function call");
            return node;
        }
        node->children.push_back(std::move(callExpr));
        ExpectOp(";");
        return node;
    }

    // Threading blueprint step 2: `wait <expr>;`.
    std::unique_ptr<KisakAstNode> ParseWait() {
        uint32_t line = CurLine();
        Advance();  // 'wait'
        auto node = MakeNode(Kind::WaitStatement, line);
        node->children.push_back(ParseExpression());
        ExpectOp(";");
        return node;
    }

    std::unique_ptr<KisakAstNode> ParseIf() {
        uint32_t line = CurLine();
        Advance();  // 'if'
        auto node = MakeNode(Kind::IfStatement, line);
        if (!ExpectOp("(")) return node;
        auto cond = ParseExpression();
        node->children.push_back(std::move(cond));
        if (!ExpectOp(")")) return node;
        auto thenBranch = ParseStatement();
        node->children.push_back(std::move(thenBranch));
        if (MatchKeyword(KisakScriptKeyword::Else)) {
            auto elseBranch = ParseStatement();
            node->children.push_back(std::move(elseBranch));
        }
        return node;
    }

    std::unique_ptr<KisakAstNode> ParseWhile() {
        uint32_t line = CurLine();
        Advance();  // 'while'
        auto node = MakeNode(Kind::WhileStatement, line);
        if (!ExpectOp("(")) return node;
        auto cond = ParseExpression();
        node->children.push_back(std::move(cond));
        if (!ExpectOp(")")) return node;
        auto body = ParseStatement();
        node->children.push_back(std::move(body));
        return node;
    }

    std::unique_ptr<KisakAstNode> ParseFor() {
        uint32_t line = CurLine();
        Advance();  // 'for'
        auto node = MakeNode(Kind::ForStatement, line);
        if (!ExpectOp("(")) return node;

        node->children.push_back(CheckOp(";") ? nullptr : ParseExpressionOrAssignmentExpr());
        if (!ExpectOp(";")) return node;

        node->children.push_back(CheckOp(";") ? nullptr : ParseExpression());
        if (!ExpectOp(";")) return node;

        node->children.push_back(CheckOp(")") ? nullptr : ParseExpressionOrAssignmentExpr());
        if (!ExpectOp(")")) return node;

        node->children.push_back(ParseStatement());
        return node;
    }

    std::unique_ptr<KisakAstNode> ParseReturn() {
        uint32_t line = CurLine();
        Advance();  // 'return'
        auto node = MakeNode(Kind::ReturnStatement, line);
        if (!CheckOp(";")) {
            node->children.push_back(ParseExpression());
        }
        ExpectOp(";");
        return node;
    }

    // A statement that's either an assignment (`target = expr;`, including
    // compound `+=`/`-=`/`*=`/`/=`/`%=`) or a bare/method call expression
    // used for its side effect (`precacheModel(x);`, `self setModel(x);`).
    std::unique_ptr<KisakAstNode> ParseExpressionOrAssignmentStatement() {
        uint32_t line = CurLine();
        auto expr = ParseExpressionOrAssignmentExpr();
        if (failed_) return nullptr;
        // Entity/object-model blueprint step 3: MethodThreadCallStatement is
        // returned as its own statement kind (like bare ThreadCallStatement
        // at the top of ParseStatement), not wrapped in ExpressionStatement
        // -- same treatment as Assignment above, extended by one clause.
        if (expr && (expr->kind == Kind::Assignment ||
                     expr->kind == Kind::MethodThreadCallStatement)) {
            ExpectOp(";");
            return expr;
        }
        auto stmt = MakeNode(Kind::ExpressionStatement, line);
        stmt->children.push_back(std::move(expr));
        ExpectOp(";");
        return stmt;
    }

    // Shared by statement-level parsing and for(;;)'s init/increment slots
    // (which are expressions, not full `;`-terminated statements) — parses
    // one of: Assignment, MethodCallExpr, or a plain expression.
    std::unique_ptr<KisakAstNode> ParseExpressionOrAssignmentExpr() {
        uint32_t line = CurLine();
        auto target = ParsePostfix();
        if (failed_) return nullptr;

        // Entity/object-model blueprint step 3: `<expr> thread <call>;` --
        // the ONLY genuinely new grammar this blueprint's parser needs. The
        // non-threaded object-prefixed form (`self set_force_color("c")`)
        // needs NO check here at all -- it already falls through to the
        // pre-existing MethodCallExpr detection below unchanged. Checked
        // BEFORE the MethodCallExpr lookahead (though order doesn't actually
        // matter functionally: MethodCallExpr's check requires an Identifier
        // token, and `thread` is a Keyword token, so the two can never both
        // match the same input) for readability -- receiver-prefixed forms
        // grouped together.
        if (CheckKeyword(KisakScriptKeyword::Thread)) {
            Advance();  // 'thread'
            auto node = MakeNode(Kind::MethodThreadCallStatement, line);
            // Reuses ParsePrimary() exactly like bare ThreadCallStatement
            // does above it -- ParsePrimary already handles both bare
            // (`funcName(args)`) and namespaced (`path\file::func(args)`)
            // call forms identically whether or not a receiver preceded
            // `thread`. Real corpus citations for this exact construct are
            // namespaced (killhouse.gsc:221, cargoship_extract.gsc:172:
            // `level thread maps\<file>::main();`).
            auto callExpr = ParsePrimary();
            if (failed_) return node;
            if (callExpr->kind != Kind::CallExpr && callExpr->kind != Kind::NamespacedCallExpr) {
                Fail("'thread' must be followed by a function call");
                return node;
            }
            node->children.push_back(std::move(target));
            node->children.push_back(std::move(callExpr));
            return node;
        }

        // Postfix ++/-- (e.g. `i++;`, real corpus usage confirmed —
        // codescripts/character.gsc). Maps directly to OP_inc/OP_dec,
        // which step 3's VM already implements against a preceding ref —
        // represented as a UnaryExpr here so step 8's compiler can lower
        // it the same way it lowers prefix unary operators.
        if (CheckOp("++") || CheckOp("--")) {
            std::string op = Advance().text;
            auto node = MakeNode(Kind::UnaryExpr, line);
            node->text = op;
            node->children.push_back(std::move(target));
            return node;
        }

        static const char* kCompoundOps[] = {"+=", "-=", "*=", "/=", "%="};
        for (const char* op : kCompoundOps) {
            if (CheckOp(op)) {
                Advance();
                auto assign = MakeNode(Kind::Assignment, line);
                assign->text = op;
                assign->children.push_back(std::move(target));
                assign->children.push_back(ParseExpression());
                return assign;
            }
        }
        if (CheckOp("=")) {
            Advance();
            auto assign = MakeNode(Kind::Assignment, line);
            assign->children.push_back(std::move(target));
            assign->children.push_back(ParseExpression());
            return assign;
        }

        // Method-call form: <object-expr> <bareword> ( args ) — no dot.
        // Only reachable here (not consumed inside ParsePostfix) because
        // it needs statement-level treatment identical to a plain call.
        if (!AtEnd() && Peek().type == KisakScriptTokenType::Identifier &&
            Peek(1).type == KisakScriptTokenType::Operator && Peek(1).text == "(") {
            std::string method = Advance().text;
            auto call = MakeNode(Kind::MethodCallExpr, line);
            call->text = method;
            call->children.push_back(std::move(target));
            ParseArgList(*call);
            return call;
        }

        // Entity/object-model blueprint step 3, Task 3: nothing above
        // matched (not an assignment, not a threaded or bare method call),
        // and the next token is one of waittill/waittillmatch/
        // waittillframeend/notify/endon -- these already fail to parse no
        // matter what (they're keywords, never Identifier-typed, so neither
        // MethodCallExpr above nor MethodThreadCallStatement above it can
        // ever accept one), but a specific, legible error here beats the
        // generic "expected ';'" ExpectOp(";") would otherwise produce back
        // in the caller. Message-quality polish only -- see
        // IsDeferredSubsystemKeyword's own comment for why this is not a
        // safety mechanism.
        if (!AtEnd() && Peek().type == KisakScriptTokenType::Keyword &&
            IsDeferredSubsystemKeyword(Peek().keyword)) {
            Fail("'" + Peek().text + "' is a deferred subsystem, not yet supported by "
                 "this grammar subset (plans/android-gscript-entity-model.md, Scope Cut item 3)");
            return target;
        }

        return target;
    }

    // ---- expressions (precedence climbing) ----

    std::unique_ptr<KisakAstNode> ParseExpression() { return ParseLogicalOr(); }

    std::unique_ptr<KisakAstNode> ParseLogicalOr() {
        auto left = ParseLogicalAnd();
        while (CheckOp("||")) {
            uint32_t line = CurLine();
            Advance();
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = "||";
            node->children.push_back(std::move(left));
            node->children.push_back(ParseLogicalAnd());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseLogicalAnd() {
        auto left = ParseEquality();
        while (CheckOp("&&")) {
            uint32_t line = CurLine();
            Advance();
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = "&&";
            node->children.push_back(std::move(left));
            node->children.push_back(ParseEquality());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseEquality() {
        auto left = ParseRelational();
        while (CheckOp("==") || CheckOp("!=")) {
            uint32_t line = CurLine();
            std::string op = Advance().text;
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = op;
            node->children.push_back(std::move(left));
            node->children.push_back(ParseRelational());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseRelational() {
        auto left = ParseAdditive();
        while (CheckOp("<") || CheckOp(">") || CheckOp("<=") || CheckOp(">=")) {
            uint32_t line = CurLine();
            std::string op = Advance().text;
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = op;
            node->children.push_back(std::move(left));
            node->children.push_back(ParseAdditive());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseAdditive() {
        auto left = ParseMultiplicative();
        while (CheckOp("+") || CheckOp("-")) {
            uint32_t line = CurLine();
            std::string op = Advance().text;
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = op;
            node->children.push_back(std::move(left));
            node->children.push_back(ParseMultiplicative());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseMultiplicative() {
        auto left = ParseUnary();
        while (CheckOp("*") || CheckOp("/") || CheckOp("%")) {
            uint32_t line = CurLine();
            std::string op = Advance().text;
            auto node = MakeNode(Kind::BinaryExpr, line);
            node->text = op;
            node->children.push_back(std::move(left));
            node->children.push_back(ParseUnary());
            left = std::move(node);
        }
        return left;
    }
    std::unique_ptr<KisakAstNode> ParseUnary() {
        if (CheckOp("!") || CheckOp("-") || CheckOp("~")) {
            uint32_t line = CurLine();
            std::string op = Advance().text;
            auto node = MakeNode(Kind::UnaryExpr, line);
            node->text = op;
            node->children.push_back(ParseUnary());
            return node;
        }
        return ParsePostfix();
    }

    // Primary expression plus a `.field`/`.size`/`[key]` postfix chain
    // (method calls are recognized one level up, at statement/assignment-rhs
    // scope, since they need to know they're not inside a nested
    // sub-expression position in this subset — matching real GSC's own
    // restriction that method calls are themselves statement-level, not
    // nested as a general sub-expression, e.g. `x = self setModel(y);` is
    // not valid GSC in the first place).
    //
    // Arrays blueprint (plans/android-gscript-arrays.md) step 2 added `[key]`
    // (chainable, e.g. `a[i][j]`) and `.size`. `.size` is deliberately its own
    // node kind (ArraySizeExpr), not FieldAccessExpr — see the node kind's own
    // doc comment for why keeping it separate from the deferred entity-field
    // boundary matters. Real corpus: `.size` is always a bare property read,
    // never `.size(...)` — if a `(` ever follows the identifier `size`, fall
    // through to ordinary FieldAccessExpr instead (consistent with how every
    // other field name is handled here; this function never itself looks for
    // a trailing call, that's the one-level-up job described above).
    std::unique_ptr<KisakAstNode> ParsePostfix() {
        auto expr = ParsePrimary();
        while (!failed_) {
            if (CheckOp(".")) {
                uint32_t line = CurLine();
                Advance();
                std::string field;
                if (!ExpectIdentifier(field)) return expr;
                if (field == "size" && !CheckOp("(")) {
                    auto node = MakeNode(Kind::ArraySizeExpr, line);
                    node->children.push_back(std::move(expr));
                    expr = std::move(node);
                    continue;
                }
                auto node = MakeNode(Kind::FieldAccessExpr, line);
                node->text = field;
                node->children.push_back(std::move(expr));
                expr = std::move(node);
                continue;
            }
            if (CheckOp("[")) {
                uint32_t line = CurLine();
                Advance();
                auto key = ParseExpression();
                if (!ExpectOp("]")) return expr;
                auto node = MakeNode(Kind::ArrayIndexExpr, line);
                node->children.push_back(std::move(expr));
                node->children.push_back(std::move(key));
                expr = std::move(node);
                continue;
            }
            break;
        }
        return expr;
    }

    void ParseArgList(KisakAstNode& call) {
        if (!ExpectOp("(")) return;
        if (!CheckOp(")")) {
            call.children.push_back(ParseExpression());
            while (MatchOp(",")) {
                call.children.push_back(ParseExpression());
            }
        }
        ExpectOp(")");
    }

    // Namespaced-calls blueprint step 2: called once the `::` prefix and the
    // function name have both already been consumed (pathSegments may be
    // empty for a bare `::func` reference with no path). Disambiguates the
    // call form (`(args)` follows) from the bare value-reference form
    // (`default_start( ::inside_start )`, killhouse.gsc line 27) by a
    // single-token lookahead, same pattern ParsePrimary's plain Identifier
    // case already uses for CallExpr vs IdentifierExpr.
    std::unique_ptr<KisakAstNode> ParseNamespacedTail(
        uint32_t line, std::vector<std::string> pathSegments, const std::string& funcName) {
        if (CheckOp("(")) {
            auto call = MakeNode(Kind::NamespacedCallExpr, line);
            call->text = funcName;
            call->stringList = std::move(pathSegments);
            ParseArgList(*call);
            return call;
        }
        auto node = MakeNode(Kind::FunctionRefExpr, line);
        node->text = funcName;
        node->stringList = std::move(pathSegments);
        return node;
    }

    std::unique_ptr<KisakAstNode> ParsePrimary() {
        uint32_t line = CurLine();
        if (AtEnd()) { Fail("unexpected end of input"); return MakeNode(Kind::UndefinedLiteralExpr, line); }

        const KisakScriptToken& tok = Peek();
        switch (tok.type) {
            case KisakScriptTokenType::IntLiteral: {
                Advance();
                auto node = MakeNode(Kind::IntLiteralExpr, line);
                node->intValue = tok.intValue;
                return node;
            }
            case KisakScriptTokenType::FloatLiteral: {
                Advance();
                auto node = MakeNode(Kind::FloatLiteralExpr, line);
                node->floatValue = tok.floatValue;
                return node;
            }
            case KisakScriptTokenType::StringLiteral: {
                Advance();
                auto node = MakeNode(Kind::StringLiteralExpr, line);
                node->text = tok.stringValue;
                return node;
            }
            case KisakScriptTokenType::IStringLiteral: {
                Advance();
                auto node = MakeNode(Kind::IStringLiteralExpr, line);
                node->text = tok.stringValue;
                return node;
            }
            case KisakScriptTokenType::Identifier: {
                std::string name = Advance().text;
                if (CheckOp("\\")) {
                    // Namespaced call/reference: path\...\file::func(...) or
                    // path\...\file::func (bare value). Real corpus uses 1+
                    // path segments (e.g. maps\_blackhawk::main(), and
                    // maps\createart\killhouse_art::main() with 2) — collect
                    // all of them before expecting the `::`.
                    std::vector<std::string> pathSegments;
                    pathSegments.push_back(name);
                    while (MatchOp("\\")) {
                        std::string segment;
                        if (!ExpectIdentifier(segment)) return MakeNode(Kind::UndefinedLiteralExpr, line);
                        pathSegments.push_back(segment);
                    }
                    if (!ExpectOp("::")) return MakeNode(Kind::UndefinedLiteralExpr, line);
                    std::string funcName;
                    if (!ExpectIdentifier(funcName)) return MakeNode(Kind::UndefinedLiteralExpr, line);
                    return ParseNamespacedTail(line, std::move(pathSegments), funcName);
                }
                if (CheckOp("(")) {
                    // Bare call: precacheModel(...), add(a, b), etc.
                    auto call = MakeNode(Kind::CallExpr, line);
                    call->text = name;
                    ParseArgList(*call);
                    return call;
                }
                auto node = MakeNode(Kind::IdentifierExpr, line);
                node->text = name;
                return node;
            }
            case KisakScriptTokenType::Keyword: {
                switch (tok.keyword) {
                    case KisakScriptKeyword::KwTrue: {
                        Advance();
                        auto node = MakeNode(Kind::BoolLiteralExpr, line);
                        node->intValue = 1;
                        return node;
                    }
                    case KisakScriptKeyword::KwFalse: {
                        Advance();
                        auto node = MakeNode(Kind::BoolLiteralExpr, line);
                        node->intValue = 0;
                        return node;
                    }
                    case KisakScriptKeyword::Undefined:
                        Advance();
                        return MakeNode(Kind::UndefinedLiteralExpr, line);
                    default:
                        if (IsObjectKeyword(tok.keyword)) {
                            Advance();
                            auto node = MakeNode(Kind::IdentifierExpr, line);
                            node->text = tok.text;
                            return node;
                        }
                        Fail("unexpected keyword '" + tok.text + "' in expression (not supported by this grammar subset)");
                        return MakeNode(Kind::UndefinedLiteralExpr, line);
                }
            }
            case KisakScriptTokenType::Operator:
                if (tok.text == "(") {
                    Advance();
                    auto inner = ParseExpression();
                    ExpectOp(")");
                    return inner;
                }
                if (tok.text == "::") {
                    // Bare function pointer, no path prefix: `::inside_start`
                    // (killhouse.gsc line 27, used as a value — the common
                    // real-corpus form) or, for completeness, `::func(...)`
                    // as an immediate call. Empty pathSegments distinguishes
                    // this from the path-prefixed form in ParseNamespacedTail.
                    Advance();
                    std::string funcName;
                    if (!ExpectIdentifier(funcName)) return MakeNode(Kind::UndefinedLiteralExpr, line);
                    return ParseNamespacedTail(line, {}, funcName);
                }
                if (tok.text == "[") {
                    // Arrays blueprint step 2: `[]` is the ONLY array-literal
                    // form (retail has no populated-literal opcode, and no
                    // `[1,2,3]`-style syntax appears anywhere in the real
                    // corpus). A `[` reaching THIS point (where a primary
                    // expression is expected) is only ever the literal, never
                    // a subscript — subscript (`base[key]`) is recognized as a
                    // postfix operator in ParsePostfix, one level up, only
                    // after another expression has already been parsed.
                    Advance();
                    if (MatchOp("]")) {
                        return MakeNode(Kind::ArrayLiteralExpr, line);
                    }
                    Fail("populated array literals ('[expr, ...]') don't exist in real GSC "
                         "and are not supported (plans/android-gscript-arrays.md) — "
                         "build the array with [] then assign each element by key");
                    return MakeNode(Kind::UndefinedLiteralExpr, line);
                }
                Fail("unexpected operator '" + tok.text + "' in expression");
                return MakeNode(Kind::UndefinedLiteralExpr, line);
            default:
                Fail("unexpected token in expression");
                return MakeNode(Kind::UndefinedLiteralExpr, line);
        }
    }
};

}  // namespace

KisakScriptParseResult ParseGscTokens(const std::vector<KisakScriptToken>& tokens) {
    Parser parser(tokens);
    return parser.Run();
}

KisakScriptParseResult ParseGscSource(const std::string& source) {
    KisakScriptLexResult lex = TokenizeGscSource(source);
    if (!lex.errors.empty()) {
        KisakScriptParseResult result;
        result.errors = lex.errors;
        return result;
    }
    return ParseGscTokens(lex.tokens);
}

std::string DescribeAstNodeKind(KisakAstNodeKind kind) {
    switch (kind) {
        case Kind::Program: return "Program";
        case Kind::FunctionDef: return "FunctionDef";
        case Kind::Block: return "Block";
        case Kind::IfStatement: return "IfStatement";
        case Kind::WhileStatement: return "WhileStatement";
        case Kind::ForStatement: return "ForStatement";
        case Kind::ReturnStatement: return "ReturnStatement";
        case Kind::ExpressionStatement: return "ExpressionStatement";
        case Kind::SwitchStatement: return "SwitchStatement";
        case Kind::CaseClause: return "CaseClause";
        case Kind::BreakStatement: return "BreakStatement";
        case Kind::ContinueStatement: return "ContinueStatement";
        case Kind::ThreadCallStatement: return "ThreadCallStatement";
        case Kind::WaitStatement: return "WaitStatement";
        case Kind::Assignment: return "Assignment";
        case Kind::BinaryExpr: return "BinaryExpr";
        case Kind::UnaryExpr: return "UnaryExpr";
        case Kind::CallExpr: return "CallExpr";
        case Kind::MethodCallExpr: return "MethodCallExpr";
        case Kind::MethodThreadCallStatement: return "MethodThreadCallStatement";
        case Kind::NamespacedCallExpr: return "NamespacedCallExpr";
        case Kind::FunctionRefExpr: return "FunctionRefExpr";
        case Kind::FieldAccessExpr: return "FieldAccessExpr";
        case Kind::ArrayLiteralExpr: return "ArrayLiteralExpr";
        case Kind::ArrayIndexExpr: return "ArrayIndexExpr";
        case Kind::ArraySizeExpr: return "ArraySizeExpr";
        case Kind::IdentifierExpr: return "IdentifierExpr";
        case Kind::IntLiteralExpr: return "IntLiteralExpr";
        case Kind::FloatLiteralExpr: return "FloatLiteralExpr";
        case Kind::StringLiteralExpr: return "StringLiteralExpr";
        case Kind::IStringLiteralExpr: return "IStringLiteralExpr";
        case Kind::BoolLiteralExpr: return "BoolLiteralExpr";
        case Kind::UndefinedLiteralExpr: return "UndefinedLiteralExpr";
    }
    return "?";
}

std::string DumpAst(const KisakAstNode& node, int indent) {
    std::string out(static_cast<size_t>(indent) * 2, ' ');
    out += DescribeAstNodeKind(node.kind);
    if (!node.text.empty()) out += " '" + node.text + "'";
    if (node.kind == KisakAstNodeKind::IntLiteralExpr) out += " " + std::to_string(node.intValue);
    if (node.kind == KisakAstNodeKind::FloatLiteralExpr) out += " " + std::to_string(node.floatValue);
    if (node.kind == KisakAstNodeKind::CaseClause && node.isDefault) out += " default";
    if (node.kind == KisakAstNodeKind::BoolLiteralExpr) out += node.intValue ? " true" : " false";
    if (!node.stringList.empty()) {
        bool isPath = node.kind == KisakAstNodeKind::NamespacedCallExpr ||
                      node.kind == KisakAstNodeKind::FunctionRefExpr;
        out += isPath ? " path=(" : " params=(";
        for (size_t i = 0; i < node.stringList.size(); ++i) {
            if (i) out += isPath ? "\\" : ", ";
            out += node.stringList[i];
        }
        out += ")";
    }
    out += " [line " + std::to_string(node.line) + "]\n";
    for (const auto& child : node.children) {
        out += child ? DumpAst(*child, indent + 1)
                      : std::string(static_cast<size_t>(indent + 1) * 2, ' ') + "<null>\n";
    }
    return out;
}
