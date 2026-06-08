#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/AssignExpr.h"
#include "../ast/Expressions/BinaryExpr.h"
#include "../ast/Expressions/CastExpr.h"
#include "../ast/Expressions/FloatingLiterals.h"
#include "../ast/Expressions/FunctionCallExpr.h"
#include "../ast/Expressions/InitExpr.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Expressions/MemberExpr.h"
#include "../ast/Expressions/SizeOfExpr.h"
#include "../ast/Expressions/StringLiterals.h"
#include "../ast/Expressions/SubscriptExpr.h"
#include "../ast/Expressions/TernaryExpr.h"
#include "../ast/Expressions/UnaryExpr.h"
#include "../ast/Expressions/VariableExpr.h"
#include "../ast/Statements/BreakStmt.h"
#include "../ast/Statements/ContinueStmt.h"
#include "../ast/Statements/DeclareStmt.h"
#include "../ast/Statements/DoWhileStmt.h"
#include "../ast/Statements/ExprStmt.h"
#include "../ast/Statements/ForStmt.h"
#include "../ast/Statements/FunctionDeclStmt.h"
#include "../ast/Statements/IfStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/Statements/WhileStmt.h"
#include "../ast/StorageClass.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../ast/TopLevelNodes/StructDecl.h"
#include "../lexer/token.h"
#include "../support/RTTI.h"
#include "../utils/diagnostic.h"

// The result of resolving a declarator against a base type: the declared name,
// the fully-derived type, and (for function declarators) the parameter list.
struct Declarator
{
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int col;
    std::vector<Parameter> params;
};

// ============================================================
// Declarator grammar tree
//
// A declarator is parsed into this small recursive tree first, then resolved
// against a base type by processDeclarator(). Parsing the shape separately from
// resolving the type is what makes parenthesized declarators work: in
// `int (*p)[10]` the grouping puts the pointer closer to the leaf than the array
// suffix, so p comes out as "pointer to array" rather than "array of pointer".
// An IdentDeclarator with an empty name is an abstract declarator (casts/sizeof,
// and unnamed parameters).
// ============================================================
struct DeclaratorNode
{
    virtual ~DeclaratorNode() = default;
};

struct IdentDeclarator : DeclaratorNode
{
    std::string name;
    int line = -1;
    int col = -1;
    IdentDeclarator() = default;
    IdentDeclarator(std::string name_, int line_, int col_)
        : name(std::move(name_)), line(line_), col(col_)
    {
    }
};

struct PointerDeclarator : DeclaratorNode
{
    std::shared_ptr<DeclaratorNode> inner;
    explicit PointerDeclarator(std::shared_ptr<DeclaratorNode> inner_) : inner(std::move(inner_)) {}
};

struct ArrayDeclarator : DeclaratorNode
{
    std::shared_ptr<DeclaratorNode> inner;
    size_t size;
    ArrayDeclarator(std::shared_ptr<DeclaratorNode> inner_, size_t size_)
        : inner(std::move(inner_)), size(size_)
    {
    }
};

// A parameter as it appears inside a function declarator: its own base type plus
// its (possibly abstract) declarator, resolved later by processDeclarator().
struct ParamInfo
{
    std::shared_ptr<Type> baseType;
    std::shared_ptr<DeclaratorNode> declarator;
    int line;
    int col;
};

struct FunDeclarator : DeclaratorNode
{
    std::vector<ParamInfo> params;
    bool variadic;
    std::shared_ptr<DeclaratorNode> inner;
    FunDeclarator(std::vector<ParamInfo> params_, bool variadic_,
                  std::shared_ptr<DeclaratorNode> inner_)
        : params(std::move(params_)), variadic(variadic_), inner(std::move(inner_))
    {
    }
};

// Internal exception used only to unwind deep parse recursion on error.
// Never escapes the Parser — caught at the two recovery boundaries.
struct ParseError
{
};

class Parser
{
    inline static const std::unordered_map<TokenType, int> precedenceLevel = {
        {OR_OP, 1},     {AND_OP, 2},  {PIPE, 3},      {CARET, 4},        {AMPERSAND, 5},
        {EQ_OP, 6},     {NE_OP, 6},   {LESS_THAN, 7}, {GREATER_THAN, 7}, {LE_OP, 7},
        {GE_OP, 7},     {LEFT_OP, 8}, {RIGHT_OP, 8},  {PLUS, 9},         {MINUS, 9},
        {ASTERISK, 10}, {SLASH, 10},  {PERCENT, 10}};
    std::vector<Token> tokens;
    int cur_token = 0;
    Diagnostic::DiagnosticEngine &diagnosticEngine;

  public:
    Parser(std::vector<Token> tokens_, Diagnostic::DiagnosticEngine &eng)
        : tokens(std::move(tokens_)), diagnosticEngine(eng)
    {
    }

    // ============================================================
    // Cursor helpers — peek, consume, expect
    // ============================================================

    TokenType peek()
    {
        if (cur_token == (int)tokens.size())
            return EOF_TOKEN;
        return tokens[cur_token].type;
    }

    TokenType peekNext()
    {
        if (cur_token + 1 >= (int)tokens.size())
            return EOF_TOKEN;
        return tokens[cur_token + 1].type;
    }

    Token consume() { return tokens[cur_token++]; }

    // The token the cursor is on, clamped to the last token so error reporting at
    // end-of-input still has a position to point at.
    const Token &curToken() const { return tokens[std::min(cur_token, (int)tokens.size() - 1)]; }

    // ============================================================
    // Error reporting — every parse error funnels through here so the
    // "line L, col C: message" format stays consistent.
    // Reports via the diagnostic engine then throws ParseError to unwind;
    // caught at the two recovery boundaries in ParseProgram/parseBlockStmt.
    // ============================================================

    [[noreturn]] void error(int line, int col, const std::string &message)
    {
        diagnosticEngine.report(Diagnostic::DiagLevel::PARSER, {line, col}, message);
        throw ParseError{};
    }

    [[noreturn]] void errorAt(const Token &tok, const std::string &message)
    {
        error(tok.line, tok.col, message);
    }

    [[noreturn]] void error(const std::string &message) { errorAt(curToken(), message); }

    Token expect(TokenType type)
    {
        if (peek() != type)
            error("expected " + std::string(tokenTypeToString(type)) + ", got " +
                  std::string(tokenTypeToString(peek())));
        return consume();
    }

    // ============================================================
    // Panic-mode recovery — advance to a safe resync point.
    // Always consumes at least one token to guarantee progress.
    // Stops when:
    //   - The just-consumed token was ';'  (end of a statement)
    //   - The current token starts a new statement/declaration
    //   - EOF
    // ============================================================
    void synchronize()
    {
        // Consume at least one token unconditionally to guarantee forward progress.
        if (peek() == EOF_TOKEN)
            return;
        Token prev = consume();

        while (peek() != EOF_TOKEN)
        {
            // Previous token was a statement terminator — resync after it.
            if (prev.type == SEMI_COLON)
                return;

            // Current token starts a fresh declaration or statement.
            const TokenType cur = peek();
            if (isDeclSpecifierStart(cur) || cur == LEFT_BRACE || cur == RIGHT_BRACE || cur == IF ||
                cur == FOR || cur == WHILE || cur == DO || cur == RETURN || cur == BREAK ||
                cur == CONTINUE)
                return;

            prev = consume();
        }
    }

    // ============================================================
    // Predicate helpers — type starts and operator categories
    // ============================================================

    static bool isTypeStart(const TokenType t)
    {
        return t == INT || t == CHAR || t == VOID || t == STRUCT || t == LONG || t == UNSIGNED ||
               t == SIGNED || t == DOUBLE;
    }

    static bool isStorageClassSpecifier(const TokenType t) { return t == STATIC || t == EXTERN; }

    // A declaration can begin with either a type specifier or a storage class,
    // since C lets them appear in any order (`static int` or `int static`).
    static bool isDeclSpecifierStart(const TokenType t)
    {
        return isTypeStart(t) || isStorageClassSpecifier(t);
    }

    bool isUnaryOp(TokenType type)
    {
        return type == EXCLAMATION || type == TILDE || type == MINUS || type == PLUS ||
               type == AMPERSAND || type == ASTERISK || type == INC_OP || type == DEC_OP;
    }

    bool isBinaryOp(TokenType type)
    {
        return type == OR_OP || type == AND_OP || type == PIPE || type == CARET ||
               type == AMPERSAND || type == EQ_OP || type == NE_OP || type == LESS_THAN ||
               type == GREATER_THAN || type == LE_OP || type == GE_OP || type == LEFT_OP ||
               type == RIGHT_OP || type == PLUS || type == MINUS || type == ASTERISK ||
               type == SLASH || type == PERCENT;
    }

    bool isAssignmentOp(TokenType type)
    {
        return type == ASSIGN || type == ADD_ASSIGN || type == SUB_ASSIGN || type == MUL_ASSIGN ||
               type == DIV_ASSIGN || type == MOD_ASSIGN || type == AND_ASSIGN ||
               type == OR_ASSIGN || type == XOR_ASSIGN || type == LEFT_ASSIGN ||
               type == RIGHT_ASSIGN;
    }

    // A CONSTANT token whose lexeme opens with a single quote is a character
    // constant ('a', '\n', ...) rather than a numeric constant.
    static bool isCharConstantLexeme(const std::string &lex)
    {
        return !lex.empty() && lex.front() == '\'';
    }

    // Decode a character-constant lexeme (quotes included) to its value. A
    // character constant has type int, and plain char is signed on this target,
    // so the byte is sign-extended (e.g. '\xff' would be -1).
    static int decodeCharConstant(const std::string &lex)
    {
        const std::string body = lex.substr(1, lex.size() - 2);
        unsigned char value;
        if (body.size() >= 2 && body[0] == '\\')
        {
            switch (body[1])
            {
            case 'n':
                value = '\n';
                break;
            case 't':
                value = '\t';
                break;
            case 'r':
                value = '\r';
                break;
            case 'a':
                value = '\a';
                break;
            case 'b':
                value = '\b';
                break;
            case 'f':
                value = '\f';
                break;
            case 'v':
                value = '\v';
                break;
            case '\\':
                value = '\\';
                break;
            case '\'':
                value = '\'';
                break;
            case '"':
                value = '"';
                break;
            case '?':
                value = '\?';
                break;
            case '0':
                value = '\0';
                break;
            default:
                value = static_cast<unsigned char>(body[1]);
                break;
            }
        }
        else
        {
            value = static_cast<unsigned char>(body[0]);
        }
        return static_cast<int>(static_cast<signed char>(value));
    }

    // ============================================================
    // Type / declarator parsing — base types and declarator chains
    // ============================================================

    // Map the distinct type-specifier tokens gathered for one declaration to the
    // Type they denote, or throw on an illegal combination. Duplicates are already
    // rejected by the caller, so each key appears at most once. `structName` holds
    // the tag token when a `struct` specifier is present; line/col locate the run.
    std::shared_ptr<Type> typeFromSpecifiers(const std::unordered_map<TokenType, Token> &specs,
                                             const std::optional<Token> &structName, int line,
                                             int col)
    {
        const auto has = [&](TokenType t) { return specs.contains(t); };
        const auto invalid = [&]() -> std::shared_ptr<Type>
        { error(line, col, "invalid combination of type specifiers"); };

        // `signed` and `unsigned` are mutually exclusive.
        if (has(SIGNED) && has(UNSIGNED))
            return invalid();

        // char is the only integer base that combines with signed/unsigned but
        // nothing else: `char`, `signed char`, `unsigned char` are three distinct
        // types and char cannot mix with int/long/double/void/struct.
        if (has(CHAR))
        {
            if (has(VOID) || has(STRUCT) || has(INT) || has(LONG) || has(DOUBLE))
                return invalid();
            if (has(SIGNED))
                return SignedCharType::getInstance();
            if (has(UNSIGNED))
                return UnsignedCharType::getInstance();
            return CharType::getInstance();
        }

        // void / struct are standalone: they combine with nothing else.
        if (has(VOID) || has(STRUCT))
        {
            if (specs.size() != 1)
                return invalid();
            if (has(VOID))
                return VoidType::getInstance();
            return std::make_shared<StructType>(structName->lexeme);
        }

        if (has(DOUBLE))
        {
            if (has(SIGNED) || has(UNSIGNED) || has(LONG) || has(INT))
                return invalid();
            return DoubleType::getInstance();
        }

        // What remains is some combination of unsigned / int / long; long subsumes
        // int, and unsigned takes precedence in choosing the type.
        if (has(UNSIGNED))
        {
            if (has(LONG))
                return UnsignedLongType::getInstance();
            return UnsignedIntType::getInstance();
        }
        if (has(LONG))
            return LongType::getInstance();
        if (has(INT))
            return IntType::getInstance();
        // Bare `signed` == int (`signed int` / `signed long` already resolved above).
        if (has(SIGNED))
            return IntType::getInstance();

        return invalid();
    }

    // A run of declaration specifiers resolved to a base type plus optional storage
    // class, with the position of the first specifier token.
    struct Specifiers
    {
        std::shared_ptr<Type> type;
        std::optional<StorageClass> storageClass;
        int line;
        int col;
    };

    // Gather a run of type-specifier keywords (in any order), plus storage classes
    // when allowStorageClass is set, and resolve them to a single base type.
    Specifiers parseSpecifiers(bool allowStorageClass)
    {
        std::optional<StorageClass> storageClass;
        std::unordered_map<TokenType, Token> specs;
        std::optional<Token> structName;
        int line = -1, col = -1;

        while (isDeclSpecifierStart(peek()))
        {
            Token t = consume();
            if (line < 0)
            {
                line = t.line;
                col = t.col;
            }

            if (isStorageClassSpecifier(t.type))
            {
                if (!allowStorageClass)
                    errorAt(t, "storage-class specifier is not allowed here");
                if (storageClass.has_value())
                    errorAt(t, "multiple storage-class specifiers");
                storageClass = (t.type == STATIC) ? StorageClass::Static : StorageClass::Extern;
            }
            else
            {
                if (!specs.try_emplace(t.type, t).second)
                    errorAt(t, "type specifier used more than once");
                if (t.type == STRUCT)
                    structName = expect(IDENTIFIER);
            }
        }

        if (specs.empty())
            error("expected a type specifier, got " + std::string(tokenTypeToString(peek())));

        return {typeFromSpecifiers(specs, structName, line, col), storageClass, line, col};
    }

    // Used wherever a type name appears without a storage class: casts, sizeof,
    // parameters, struct fields.
    std::tuple<std::shared_ptr<Type>, int, int> parseBaseType()
    {
        Specifiers s = parseSpecifiers(false);
        return {s.type, s.line, s.col};
    }

    // Declaration-specifiers: a base type plus an optional storage class, in any
    // order (`static int` / `int static`).
    std::tuple<std::shared_ptr<Type>, std::optional<StorageClass>, int, int> parseDeclSpecifiers()
    {
        Specifiers s = parseSpecifiers(true);
        return {s.type, s.storageClass, s.line, s.col};
    }

    // declarator = "*" declarator | direct-declarator
    std::shared_ptr<DeclaratorNode> parseDeclarator(bool allowAbstract)
    {
        if (peek() == ASTERISK)
        {
            consume();
            return std::make_shared<PointerDeclarator>(parseDeclarator(allowAbstract));
        }
        return parseDirectDeclarator(allowAbstract);
    }

    // direct-declarator = simple-declarator [ param-list | "[" const "]" ... ]
    std::shared_ptr<DeclaratorNode> parseDirectDeclarator(bool allowAbstract)
    {
        std::shared_ptr<DeclaratorNode> node = parseSimpleDeclarator(allowAbstract);
        if (peek() == LEFT_PAREN)
        {
            auto [params, variadic] = parseParamList();
            return std::make_shared<FunDeclarator>(std::move(params), variadic, std::move(node));
        }
        while (peek() == LEFT_BRACKET)
        {
            consume();
            Token sz = expect(CONSTANT);
            expect(RIGHT_BRACKET);
            const size_t dim = isCharConstantLexeme(sz.lexeme)
                                   ? static_cast<size_t>(decodeCharConstant(sz.lexeme))
                                   : std::stoull(sz.lexeme);
            node = std::make_shared<ArrayDeclarator>(std::move(node), dim);
        }
        return node;
    }

    // simple-declarator = identifier | "(" declarator ")"
    // A "(" only opens a grouping when what follows can start a declarator (*, (,
    // or a name); otherwise it belongs to a function param list handled above. In
    // an abstract declarator the grouped declarator may itself start with "[" (an
    // abstract array, e.g. the "([3][4])" in "double (*([3][4]))[2]"), which no
    // parameter list can, so "[" also opens a grouping there.
    std::shared_ptr<DeclaratorNode> parseSimpleDeclarator(bool allowAbstract)
    {
        if (peek() == LEFT_PAREN &&
            (peekNext() == ASTERISK || peekNext() == LEFT_PAREN || peekNext() == IDENTIFIER ||
             (allowAbstract && peekNext() == LEFT_BRACKET)))
        {
            consume();
            auto inner = parseDeclarator(allowAbstract);
            expect(RIGHT_PAREN);
            return inner;
        }
        if (peek() == IDENTIFIER)
        {
            Token id = consume();
            return std::make_shared<IdentDeclarator>(id.lexeme, id.line, id.col);
        }
        if (allowAbstract)
            return std::make_shared<IdentDeclarator>();
        error("expected identifier in declarator");
    }

    // param-list = "(" [ "void" | param { "," param } [ "," "..." ] ] ")"
    std::pair<std::vector<ParamInfo>, bool> parseParamList()
    {
        expect(LEFT_PAREN);
        std::vector<ParamInfo> params;
        bool variadic = false;

        if (peek() == VOID && peekNext() == RIGHT_PAREN)
        {
            consume(); // void
        }
        else if (peek() != RIGHT_PAREN)
        {
            while (true)
            {
                if (peek() == ELLIPSIS)
                {
                    if (params.empty())
                        error("there must be at least one named parameter before '...'");
                    consume();
                    variadic = true;
                    break;
                }
                auto [baseType, line, col] = parseBaseType();
                auto decl = parseDeclarator(true); // parameters may be abstract
                params.push_back(ParamInfo{baseType, decl, line, col});
                if (peek() == COMMA)
                {
                    consume();
                    continue;
                }
                break;
            }
        }
        expect(RIGHT_PAREN);
        return {std::move(params), variadic};
    }

    // Resolve a declarator tree against a base type, deriving outermost-first so
    // the type reads from the leaf out (the natural C declarator reading).
    Declarator processDeclarator(const std::shared_ptr<DeclaratorNode> &node,
                                 const std::shared_ptr<Type> &baseType)
    {
        if (auto id = std::dynamic_pointer_cast<IdentDeclarator>(node))
            return Declarator{baseType, id->name, id->line, id->col, {}};
        if (auto p = std::dynamic_pointer_cast<PointerDeclarator>(node))
            return processDeclarator(p->inner, std::make_shared<PointerType>(baseType));
        if (auto a = std::dynamic_pointer_cast<ArrayDeclarator>(node))
            return processDeclarator(a->inner, std::make_shared<ArrayType>(baseType, a->size));

        auto f = std::dynamic_pointer_cast<FunDeclarator>(node);
        // The thing being declared as a function must be a plain identifier; a
        // non-identifier inner means a function pointer or a function returning a
        // function, neither of which this compiler supports.
        auto idInner = std::dynamic_pointer_cast<IdentDeclarator>(f->inner);
        if (!idInner)
            error("function pointers and functions returning functions are not supported");

        std::vector<Parameter> params;
        std::vector<std::shared_ptr<Type>> paramTypes;
        for (const auto &pinfo : f->params)
        {
            Declarator pd = processDeclarator(pinfo.declarator, pinfo.baseType);
            if (std::dynamic_pointer_cast<FunctionType>(pd.type))
                error("function pointers as parameters are not supported");
            const int pl = pd.name.empty() ? pinfo.line : pd.line;
            const int pc = pd.name.empty() ? pinfo.col : pd.col;
            params.emplace_back(pd.type, pd.name, pl, pc);
            paramTypes.push_back(pd.type);
        }
        auto fnType = std::make_shared<FunctionType>(baseType, paramTypes, f->variadic);
        return Declarator{fnType, idInner->name, idInner->line, idInner->col, std::move(params)};
    }

    std::shared_ptr<Type> parseAbstractDeclarator(std::shared_ptr<Type> base)
    {
        Declarator d = processDeclarator(parseDeclarator(true), base);
        if (!d.name.empty())
            error("unexpected identifier in abstract declarator");
        // Abstract function declarators (e.g. the "(void)" in "(int (void)) 0") are
        // not supported — there is no expression to cast to a function type.
        if (std::dynamic_pointer_cast<FunctionType>(d.type))
            error("abstract function declarators are not supported");
        return d.type;
    }

    // ============================================================
    // Expression parsing — top-down (entry first, leaves last)
    // ============================================================

    std::unique_ptr<Expression> parseExpression()
    {
        std::unique_ptr<Expression> left = parseAssignment();
        while (peek() == COMMA)
        {
            Token op = consume();
            int line = left->getLine(), col = left->getCol();
            std::unique_ptr<Expression> right = parseAssignment();
            left = std::make_unique<BinaryExpr>(line, col, std::move(left), std::move(right),
                                                op.lexeme);
        }
        return left;
    }

    std::unique_ptr<Expression> parseAssignment()
    {
        std::unique_ptr<Expression> left = parseTernaryExpression();
        while (isAssignmentOp(peek()))
        {
            Token op = consume();
            int line = left->getLine(), col = left->getCol();
            std::unique_ptr<Expression> right = parseAssignment();
            left = std::make_unique<AssignExpr>(std::move(left), std::move(right), op.lexeme, line,
                                                col);
        }
        return left;
    }

    std::unique_ptr<Expression> parseTernaryExpression()
    {
        std::unique_ptr<Expression> condition = parseBinaryExpression();
        if (peek() == QUESTION_MARK)
        {
            consume();
            int line = condition->getLine(), col = condition->getCol();
            std::unique_ptr<Expression> thenBranch = parseExpression();
            expect(COLON);
            std::unique_ptr<Expression> elseBranch = parseTernaryExpression();
            return std::make_unique<TernaryExpr>(std::move(condition), std::move(thenBranch),
                                                 std::move(elseBranch), line, col);
        }
        return condition;
    }

    std::unique_ptr<Expression> parseBinaryExpression(int minPrecedence = 0)
    {

        std::unique_ptr<Expression> left = parseUnary();
        while (isBinaryOp(peek()) && precedenceLevel.at(peek()) >= minPrecedence)
        {
            Token op = consume();
            int line = left->getLine(), col = left->getCol();
            std::unique_ptr<Expression> right =
                parseBinaryExpression(precedenceLevel.at(op.type) + 1);
            left = std::make_unique<BinaryExpr>(line, col, std::move(left), std::move(right),
                                                op.lexeme);
        }
        return left;
    }

    std::unique_ptr<Expression> parseUnary()
    {
        if (isUnaryOp(peek()))
        {
            Token op = consume();
            std::unique_ptr<Expression> operand = parseUnary();
            return std::make_unique<UnaryExpr>(op.line, op.col, op.lexeme, std::move(operand));
        }
        if (peek() == SIZEOF)
        {
            Token t = consume();
            if (peek() == LEFT_PAREN && isTypeStart(peekNext()))
            {
                consume();
                auto [base, _, __] = parseBaseType();
                base = parseAbstractDeclarator(base);
                expect(RIGHT_PAREN);
                return std::make_unique<SizeOfExpr>(t.line, t.col, base, nullptr);
            }
            std::unique_ptr<Expression> expr = parseUnary();
            return std::make_unique<SizeOfExpr>(expr->getLine(), expr->getCol(), nullptr,
                                                std::move(expr));
        }
        if (peek() == LEFT_PAREN && isTypeStart(peekNext()))
        {
            consume();
            auto [base, line, col] = parseBaseType();
            base = parseAbstractDeclarator(base);
            expect(RIGHT_PAREN);
            auto operand = parseUnary();
            return std::make_unique<CastExpr>(base, std::move(operand), line, col);
        }
        std::unique_ptr<Expression> operand = parseFactor();
        return operand;
    }

    std::unique_ptr<Expression> parseFactor()
    {
        std::unique_ptr<Expression> node;
        if (peek() == CONSTANT && isCharConstantLexeme(curToken().lexeme))
        {
            // A character constant has type int and its value is the decoded byte.
            Token constant = consume();
            const int value = decodeCharConstant(constant.lexeme);
            node = std::make_unique<IntLiterals>(
                constant.line, constant.col,
                static_cast<unsigned long long>(static_cast<long long>(value)),
                IntType::getInstance());
        }
        else if (peek() == CONSTANT)
        {
            Token constant = consume();
            const std::string &lex = constant.lexeme;
            const bool hasU =
                lex.find('u') != std::string::npos || lex.find('U') != std::string::npos;
            const bool hasL =
                lex.find('l') != std::string::npos || lex.find('L') != std::string::npos;
            errno = 0;
            unsigned long long value = strtoull(lex.c_str(), nullptr, 0);
            if (errno == ERANGE)
                errorAt(constant, "integer constant too large to be stored in 64 bits");
            // Pick the constant's type from its suffix and magnitude, restricted to
            // our four integer types:
            //   u + l       -> unsigned long
            //   u           -> unsigned int if it fits in 32 bits, else unsigned long
            //   l           -> long
            //   (no suffix) -> int if it fits in 32 bits, else long
            std::shared_ptr<Type> constantType;
            if (hasU && hasL)
                constantType = UnsignedLongType::getInstance();
            else if (hasU)
                constantType = value > UINT_MAX ? UnsignedLongType::getInstance()
                                                : UnsignedIntType::getInstance();
            else if (hasL)
                constantType = LongType::getInstance();
            else
                constantType = value > INT_MAX ? LongType::getInstance() : IntType::getInstance();
            node = std::make_unique<IntLiterals>(constant.line, constant.col, value, constantType);
        }
        else if (peek() == FLOATING_CONSTANT)
        {
            Token constant = consume();
            const std::string &lex = constant.lexeme;
            double value = strtod(lex.c_str(), nullptr);
            std::shared_ptr<Type> constantType = DoubleType::getInstance();
            node = std::make_unique<FloatingLiterals>(constant.line, constant.col, value,
                                                      constantType);
        }
        else if (peek() == LEFT_PAREN)
        {
            consume();
            std::unique_ptr<Expression> parseResult = parseExpression();
            expect(RIGHT_PAREN);
            node = std::move(parseResult);
        }
        else if (peek() == IDENTIFIER)
        {
            Token name = consume();
            node = std::make_unique<VariableExpr>(name.line, name.col, name.lexeme);
        }
        else if (peek() == STRING_LITERAL)
        {
            Token s = consume();
            int len = s.lexeme.size();
            std::string combined = s.lexeme.substr(1, len - 2);
            while (peek() == STRING_LITERAL)
            {
                Token r = consume();
                int newLen = r.lexeme.size();
                combined += r.lexeme.substr(1, newLen - 2);
            }
            combined = '"' + combined + '"';
            node = std::make_unique<StringLiterals>(s.line, s.col, combined);
        }
        else
            error("expected an expression, got " + std::string(tokenTypeToString(peek())));

        while (true)
        {
            if (peek() == LEFT_PAREN)
            {
                // Validate the callee is a VariableExpr. Check the kind on the raw pointer,
                // then release ownership into a unique_ptr<VariableExpr>.
                if (!isa<VariableExpr>(node.get()))
                    error("callee must be an identifier");
                // Transfer: release raw pointer, static_cast to VariableExpr*, take ownership.
                VariableExpr *raw = static_cast<VariableExpr *>(node.release());
                std::unique_ptr<VariableExpr> var(raw);
                node = parseFunctionCallExpr(std::move(var));
            }
            else if (peek() == INC_OP || peek() == DEC_OP)
            {
                Token op = consume();
                int line = node->getLine(), col = node->getCol();
                node = std::make_unique<UnaryExpr>(line, col, op.lexeme, std::move(node), true);
            }
            else if (peek() == LEFT_BRACKET)
            {
                consume();
                int line = node->getLine(), col = node->getCol();
                std::unique_ptr<Expression> index = parseExpression();
                expect(RIGHT_BRACKET);
                node =
                    std::make_unique<SubscriptExpr>(std::move(node), std::move(index), line, col);
            }
            else if (peek() == DOT || peek() == PTR_OP)
            {
                Token op = consume();
                Token field = expect(IDENTIFIER);
                int line = node->getLine(), col = node->getCol();
                node = std::make_unique<MemberExpr>(std::move(node), field.lexeme,
                                                    op.type == PTR_OP, line, col);
            }
            else
                break;
        }

        return node;
    }

    std::unique_ptr<FunctionCallExpr>
    parseFunctionCallExpr(std::unique_ptr<VariableExpr> functionName)
    {
        expect(LEFT_PAREN);
        std::vector<std::unique_ptr<Expression>> parameters;
        if (peek() != RIGHT_PAREN)
        {
            parameters.emplace_back(parseAssignment());
            while (peek() == COMMA)
            {
                consume();
                parameters.emplace_back(parseAssignment());
            }
        }
        expect(RIGHT_PAREN);
        int line = functionName->getLine(), col = functionName->getCol();
        return std::make_unique<FunctionCallExpr>(line, col, std::move(functionName),
                                                  std::move(parameters));
    }

    std::unique_ptr<Expression> parseInitializers()
    {
        if (peek() == LEFT_BRACE)
        {
            Token brace = consume();
            if (peek() == RIGHT_BRACE)
                error("empty initializer lists are not allowed");
            std::vector<std::unique_ptr<Expression>> initializations;
            if (peek() != RIGHT_BRACE)
            {
                initializations.emplace_back(parseInitializers());
                while (peek() == COMMA)
                {
                    consume();
                    if (peek() == RIGHT_BRACE)
                        break;
                    initializations.emplace_back(parseInitializers());
                }
            }
            expect(RIGHT_BRACE);
            return std::make_unique<InitExpr>(std::move(initializations), brace.line, brace.col);
        }

        return parseAssignment();
    }

    // ============================================================
    // Statement parsing — declarations, control flow, and blocks
    // ============================================================

    std::unique_ptr<ExprStmt> parseExprStatement(bool semiColon = true)
    {
        std::unique_ptr<Expression> expr = parseExpression();
        if (semiColon)
            expect(SEMI_COLON);
        int line = expr->getLine(), col = expr->getCol();
        return std::make_unique<ExprStmt>(std::move(expr), line, col, semiColon);
    }

    // A run of comma-separated declarators sharing one base type, resolving to
    // either a single function declaration/definition or a list of variables.
    struct DeclList
    {
        bool isFunction = false;
        std::unique_ptr<Function> function;
        std::vector<std::unique_ptr<VarDecl>> variables;
    };

    DeclList parseDeclarationList(const std::shared_ptr<Type> &baseType, bool global,
                                  std::optional<StorageClass> storageClass, int line, int col)
    {
        Declarator first = processDeclarator(parseDeclarator(false), baseType);

        if (auto fnType = std::dynamic_pointer_cast<FunctionType>(first.type))
        {
            std::unique_ptr<BlockStmt> body;
            if (peek() == LEFT_BRACE)
                body = parseBlockStmt();
            else
                expect(SEMI_COLON);
            auto fn =
                std::make_unique<Function>(line, col, first.name, fnType->returnType, first.params,
                                           std::move(body), fnType->isVariadic, storageClass);
            return DeclList{true, std::move(fn), {}};
        }

        std::vector<std::unique_ptr<VarDecl>> variables;
        const auto addVar = [&](const Declarator &d)
        {
            std::unique_ptr<Expression> initialization;
            if (peek() == ASSIGN)
            {
                consume();
                initialization = parseInitializers();
            }
            variables.emplace_back(std::make_unique<VarDecl>(
                d.line, d.col, d.name, d.type, std::move(initialization), global, storageClass));
        };
        addVar(first);
        while (peek() == COMMA)
        {
            consume();
            Declarator d = processDeclarator(parseDeclarator(false), baseType);
            if (std::dynamic_pointer_cast<FunctionType>(d.type))
                error("cannot mix function and variable declarations");
            addVar(d);
        }
        expect(SEMI_COLON);
        return DeclList{false, nullptr, std::move(variables)};
    }

    std::unique_ptr<DeclareStmt>
    parseDeclareStmt(std::shared_ptr<Type> &type, int line, int col,
                     std::optional<StorageClass> storageClass = std::nullopt)
    {
        if (peek() == LEFT_BRACE)
        {
            auto structVar = parseStructDecl(type, line, col);
            return std::make_unique<DeclareStmt>(line, col, std::move(structVar));
        }
        if (atStructForwardDecl(type))
            return std::make_unique<DeclareStmt>(line, col,
                                                 parseStructForwardDecl(type, line, col));
        DeclList dl = parseDeclarationList(type, false, storageClass, line, col);
        if (dl.isFunction)
            error(line, col, "function declaration not allowed here");
        return std::make_unique<DeclareStmt>(line, col, std::move(dl.variables));
    }

    // Control-flow bodies in this language are always brace-delimited blocks; the
    // brace-less single-statement forms are intentionally unsupported.
    std::unique_ptr<BlockStmt> parseRequiredBlock(const char *context)
    {
        if (peek() != LEFT_BRACE)
            error(std::string(context) + " expects a brace-delimited block");
        return parseBlockStmt();
    }

    std::unique_ptr<ReturnStmt> parseReturnStmt()
    {
        Token returnStart = expect(RETURN); // should not throw
        std::unique_ptr<Expression> returnExpression;
        if (peek() != SEMI_COLON)
            returnExpression = parseExpression();
        expect(SEMI_COLON);
        return std::make_unique<ReturnStmt>(returnStart.line, returnStart.col,
                                            std::move(returnExpression));
    }

    std::unique_ptr<IfStmt> parseIfStmt()
    {
        Token ifToken = expect(IF); // should never throw;
        expect(LEFT_PAREN);
        std::unique_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        std::unique_ptr<BlockStmt> thenBlock = parseRequiredBlock("if statement");

        std::unique_ptr<BlockStmt> elseBlock;
        if (peek() == ELSE)
        {
            consume();
            if (peek() == IF)
            {
                // `else if ...` is parsed as `else { if ... }`: parse the nested if
                // and wrap it in a synthetic block so it fits elseBlock's type.
                std::vector<std::unique_ptr<Statement>> body;
                body.emplace_back(parseIfStmt());
                elseBlock = std::make_unique<BlockStmt>(ifToken.line, ifToken.col, std::move(body));
            }
            else
            {
                elseBlock = parseRequiredBlock("else statement");
            }
        }
        return std::make_unique<IfStmt>(ifToken.line, ifToken.col, std::move(condition),
                                        std::move(thenBlock), std::move(elseBlock));
    }

    std::unique_ptr<WhileStmt> parseWhileStmt()
    {
        Token whileToken = expect(WHILE);
        expect(LEFT_PAREN);
        std::unique_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        std::unique_ptr<BlockStmt> whileBlock = parseRequiredBlock("while statement");

        return std::make_unique<WhileStmt>(whileToken.line, whileToken.col, std::move(condition),
                                           std::move(whileBlock));
    }

    std::unique_ptr<DoWhileStmt> parseDoWhileStmt()
    {
        Token doStart = expect(DO); // should not throw
        std::unique_ptr<BlockStmt> doBlock = parseRequiredBlock("do-while statement");
        expect(WHILE);
        expect(LEFT_PAREN);
        std::unique_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        expect(SEMI_COLON);
        return std::make_unique<DoWhileStmt>(std::move(doBlock), std::move(condition), doStart.line,
                                             doStart.col);
    }

    std::unique_ptr<ForStmt> parseForStmt()
    {
        Token forStart = expect(FOR);
        expect(LEFT_PAREN);
        std::unique_ptr<Statement> initialization;
        if (peek() == SEMI_COLON)
            consume();
        else if (isDeclSpecifierStart(peek()))
        {
            auto [type, storageClass, line, col] = parseDeclSpecifiers();
            // C99 6.8.5p3: a for-loop initializer declaration may only have auto
            // or register storage — static/extern are not allowed here.
            if (storageClass.has_value())
                error(line, col,
                      "a storage-class specifier is not allowed in a for-loop initializer");
            initialization = parseDeclareStmt(type, line, col);
        }
        else
            initialization = parseExprStatement(true);
        std::unique_ptr<Statement> condition;
        if (peek() == SEMI_COLON)
            consume();
        else
            condition = parseExprStatement(true);
        std::unique_ptr<Statement> update;
        if (peek() != RIGHT_PAREN)
            update = parseExprStatement(false);
        expect(RIGHT_PAREN);
        std::unique_ptr<BlockStmt> forBlock = parseRequiredBlock("for statement");
        return std::make_unique<ForStmt>(forStart.line, forStart.col, std::move(initialization),
                                         std::move(condition), std::move(update),
                                         std::move(forBlock));
    }

    std::unique_ptr<BreakStmt> parseBreakStmt()
    {
        Token breakToken = expect(BREAK);
        expect(SEMI_COLON);
        return std::make_unique<BreakStmt>(breakToken.line, breakToken.col);
    }

    std::unique_ptr<ContinueStmt> parseContinueStmt()
    {
        Token continueToken = expect(CONTINUE);
        expect(SEMI_COLON);
        return std::make_unique<ContinueStmt>(continueToken.line, continueToken.col);
    }

    std::unique_ptr<BlockStmt> parseBlockStmt()
    {
        Token blockStart = expect(LEFT_BRACE); // should never throw
        std::vector<std::unique_ptr<Statement>> statements;
        while (peek() != RIGHT_BRACE && peek() != EOF_TOKEN)
        {
            try
            {
                if (isDeclSpecifierStart(peek()))
                {
                    auto [type, storageClass, line, col] = parseDeclSpecifiers();
                    if (peek() == LEFT_BRACE)
                    {
                        auto structVar = parseStructDecl(type, line, col);
                        statements.emplace_back(
                            std::make_unique<DeclareStmt>(line, col, std::move(structVar)));
                        continue;
                    }
                    if (atStructForwardDecl(type))
                    {
                        statements.emplace_back(std::make_unique<DeclareStmt>(
                            line, col, parseStructForwardDecl(type, line, col)));
                        continue;
                    }
                    DeclList dl = parseDeclarationList(type, false, storageClass, line, col);
                    if (dl.isFunction)
                    {
                        // Local function declaration: a forward prototype scoped to this
                        // block. SA registers it and drops it at block exit; a body here
                        // would be a definition, which C forbids in block scope.
                        if (dl.function->statements)
                            error(line, col, "function definition not allowed in block scope");
                        statements.emplace_back(
                            std::make_unique<FunctionDeclStmt>(line, col, std::move(dl.function)));
                    }
                    else
                    {
                        statements.emplace_back(
                            std::make_unique<DeclareStmt>(line, col, std::move(dl.variables)));
                    }
                    continue;
                }
                if (peek() == LEFT_BRACE)
                {
                    statements.emplace_back(parseBlockStmt());
                }
                else if (peek() == SEMI_COLON)
                {
                    consume();
                }
                else if (peek() == DO)
                {
                    statements.emplace_back(parseDoWhileStmt());
                }
                else if (peek() == RETURN)
                {
                    statements.emplace_back(parseReturnStmt());
                }
                else if (peek() == IF)
                {
                    statements.emplace_back(parseIfStmt());
                }
                else if (peek() == WHILE)
                {
                    statements.emplace_back(parseWhileStmt());
                }
                else if (peek() == IDENTIFIER)
                {
                    statements.emplace_back(parseExprStatement());
                }
                else if (peek() == FOR)
                {
                    statements.emplace_back(parseForStmt());
                }
                else if (peek() == BREAK)
                {
                    statements.emplace_back(parseBreakStmt());
                }
                else if (peek() == CONTINUE)
                {
                    statements.emplace_back(parseContinueStmt());
                }
                else
                    statements.emplace_back(parseExprStatement());
            }
            catch (const ParseError &)
            {
                synchronize();
                if (peek() == RIGHT_BRACE || peek() == EOF_TOKEN)
                    break;
            }
        }
        // A block must close with `}`. If we instead ran off the end of the
        // token stream (an unterminated block), report it against the opening
        // brace and return the partial block — reporting, not throwing, so the
        // hasErrors() gate still rejects the file without unwinding further.
        if (peek() == RIGHT_BRACE)
            consume();
        else
            diagnosticEngine.report(Diagnostic::DiagLevel::PARSER,
                                    {blockStart.line, blockStart.col},
                                    "missing '}' to close this block");
        return std::make_unique<BlockStmt>(blockStart.line, blockStart.col, std::move(statements));
    }

    // ============================================================
    // Top-level / program — parameters, functions, structs, entry
    // ============================================================

    // A forward declaration `struct s;` introduces an incomplete tag with no body.
    // Only valid for a struct specifier; `int;` and friends stay declarator errors.
    std::unique_ptr<StructDecl> parseStructForwardDecl(const std::shared_ptr<Type> &structType,
                                                       int line, int col)
    {
        expect(SEMI_COLON);
        auto st = std::dynamic_pointer_cast<StructType>(structType);
        return std::make_unique<StructDecl>(st->getName(), std::vector<StructField>{}, line, col,
                                            structType, false);
    }

    // True when a run of specifiers resolved to a bare struct tag terminated by `;`,
    // i.e. a forward declaration rather than a definition or an object declaration.
    bool atStructForwardDecl(const std::shared_ptr<Type> &type)
    {
        return std::dynamic_pointer_cast<StructType>(type) && peek() == SEMI_COLON;
    }

    std::unique_ptr<StructDecl> parseStructDecl(const std::shared_ptr<Type> &structType, int line,
                                                int col)
    {
        expect(LEFT_BRACE);
        if (peek() == RIGHT_BRACE)
            error("empty struct body is not allowed");
        std::vector<StructField> fields;
        while (peek() != RIGHT_BRACE && peek() != EOF_TOKEN)
        {
            auto [type, baseLine, baseCol] = parseBaseType();
            if (peek() == SEMI_COLON)
                error(baseLine, baseCol, "struct member requires a declarator");
            while (peek() != SEMI_COLON && peek() != EOF_TOKEN)
            {
                Declarator d = processDeclarator(parseDeclarator(false), type);
                if (std::dynamic_pointer_cast<FunctionType>(d.type))
                    error(d.line, d.col,
                          "struct member '" + d.name + "' cannot have function type");
                fields.emplace_back(d.type, d.name, d.line, d.col);
                if (peek() == COMMA)
                    consume();
            }
            expect(SEMI_COLON);
        }
        expect(RIGHT_BRACE);
        expect(SEMI_COLON);
        auto actualStructType = std::dynamic_pointer_cast<StructType>(structType);
        return std::make_unique<StructDecl>(actualStructType->getName(), fields, line, col,
                                            structType);
    }

    std::unique_ptr<Program> ParseProgram()
    {
        std::vector<std::unique_ptr<TopLevelNode>> nodes;
        while (peek() != EOF_TOKEN)
        {
            try
            {
                if (isDeclSpecifierStart(peek()))
                {
                    auto [type, storageClass, line, col] = parseDeclSpecifiers();
                    if (peek() == LEFT_BRACE)
                    {
                        nodes.emplace_back(parseStructDecl(type, line, col));
                        continue;
                    }
                    if (atStructForwardDecl(type))
                    {
                        nodes.emplace_back(parseStructForwardDecl(type, line, col));
                        continue;
                    }
                    DeclList dl = parseDeclarationList(type, true, storageClass, line, col);
                    if (dl.isFunction)
                    {
                        nodes.emplace_back(std::move(dl.function));
                    }
                    else
                    {
                        for (auto &var : dl.variables)
                            nodes.emplace_back(std::move(var));
                    }
                }
                else
                {
                    error("unexpected token at file scope: " +
                          std::string(tokenTypeToString(peek())));
                }
            }
            catch (const ParseError &)
            {
                synchronize();
                // synchronize() guarantees progress; loop condition re-checks EOF.
            }
        }
        return std::make_unique<Program>(std::move(nodes));
    }
};
