#pragma once

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../ast/ASTNodes/Program.h"
#include "../ast/Expressions/AssignExpr.h"
#include "../ast/Expressions/BinaryExpr.h"
#include "../ast/Expressions/CastExpr.h"
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

struct Declarator
{
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int col;
};

class Parser
{
    std::unordered_map<TokenType, int> precedenceLevel = {
        {OR_OP, 1},     {AND_OP, 2},  {PIPE, 3},      {CARET, 4},        {AMPERSAND, 5},
        {EQ_OP, 6},     {NE_OP, 6},   {LESS_THAN, 7}, {GREATER_THAN, 7}, {LE_OP, 7},
        {GE_OP, 7},     {LEFT_OP, 8}, {RIGHT_OP, 8},  {PLUS, 9},         {MINUS, 9},
        {ASTERISK, 10}, {SLASH, 10},  {PERCENT, 10}};
    std::vector<Token> tokens;
    int cur_token = 0;

  public:
    Parser(std::vector<Token> tokens_) : tokens(std::move(tokens_)) {}

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

    Token expect(TokenType type)
    {
        if (peek() != type)
            throw std::logic_error(
                "Unexpected token at line " + std::to_string(tokens[cur_token].line) + ", col " +
                std::to_string(tokens[cur_token].col) + ", expected " + tokenTypeToString(type) +
                ", received " + tokenTypeToString(tokens[cur_token].type));
        return consume();
    }

    // ============================================================
    // Predicate helpers — type starts and operator categories
    // ============================================================

    static bool isTypeStart(const TokenType t)
    {
        return t == INT || t == CHAR || t == VOID || t == STRUCT || t == LONG || t == UNSIGNED ||
               t == SIGNED;
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

    // ============================================================
    // Type / declarator parsing — base types and declarator chains
    // ============================================================

    // Map the distinct type-specifier tokens gathered for one declaration to the
    // Type they denote, or throw on an illegal combination. Duplicates are already
    // rejected by the caller, so each key appears at most once. `structName` holds
    // the tag token when a `struct` specifier is present.
    std::shared_ptr<Type> typeFromSpecifiers(const std::unordered_map<TokenType, Token> &specs,
                                             const std::optional<Token> &structName)
    {
        const auto has = [&](TokenType t) { return specs.contains(t); };

        // `signed` and `unsigned` are mutually exclusive.
        if (has(SIGNED) && has(UNSIGNED))
            throw std::logic_error("invalid combination of type specifiers");

        // void / char / struct are standalone: they combine with nothing else.
        if (has(VOID) || has(CHAR) || has(STRUCT))
        {
            if (specs.size() != 1)
                throw std::logic_error("invalid combination of type specifiers");
            if (has(VOID))
                return VoidType::getInstance();
            if (has(CHAR))
                return CharType::getInstance();
            return std::make_shared<StructType>(structName->lexeme);
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

        throw std::logic_error("invalid combination of type specifiers");
    }

    // Gather a run of type-specifier keywords (in any order) and resolve them to a
    // single base type. Used wherever a type name appears without a storage class:
    // casts, sizeof, parameters, struct fields.
    std::tuple<std::shared_ptr<Type>, int, int> parseBaseType()
    {
        std::unordered_map<TokenType, Token> specs;
        std::optional<Token> structName;
        int line = -1, col = -1;

        while (isTypeStart(peek()))
        {
            Token t = consume();
            if (line < 0)
            {
                line = t.line;
                col = t.col;
            }
            if (!specs.try_emplace(t.type, t).second)
                throw std::logic_error("type specifier used multiple times at line:" +
                                       std::to_string(t.line) + " col:" + std::to_string(t.col));
            if (t.type == STRUCT)
                structName = expect(IDENTIFIER);
        }

        if (line < 0)
            throw std::logic_error("Expected type specifier, got " +
                                   std::string(tokenTypeToString(peek())));

        return {typeFromSpecifiers(specs, structName), line, col};
    }

    // Parse declaration-specifiers: a base type plus an optional storage class
    // (static/extern), interleaved in any order (`static int`, `int static`).
    // Returns the base type, the storage class if any, and the position of the
    // first specifier token.
    std::tuple<std::shared_ptr<Type>, std::optional<StorageClass>, int, int> parseDeclSpecifiers()
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
                if (storageClass.has_value())
                    throw std::logic_error("multiple storage-class specifiers at line " +
                                           std::to_string(t.line) + ", col " +
                                           std::to_string(t.col));
                storageClass = (t.type == STATIC) ? StorageClass::Static : StorageClass::Extern;
            }
            else
            {
                if (!specs.try_emplace(t.type, t).second)
                    throw std::logic_error(
                        "type specifier used multiple times at line:" + std::to_string(t.line) +
                        " col:" + std::to_string(t.col));
                if (t.type == STRUCT)
                    structName = expect(IDENTIFIER);
            }
        }

        if (specs.empty())
            throw std::logic_error("Expected type specifier, got " +
                                   std::string(tokenTypeToString(peek())));

        return {typeFromSpecifiers(specs, structName), storageClass, line, col};
    }

    void parseDeclaratorHead(std::shared_ptr<Type> &base)
    {
        // leading *s — leftmost is outermost, so wrap in-order
        while (peek() == ASTERISK)
        {
            consume();
            base = std::make_shared<PointerType>(base);
        }

        if (peek() == LEFT_PAREN)
        {
            throw std::logic_error(
                "parenthesized declarators (e.g. int (*p)[10]) not supported yet");
        }
    }

    void parseDeclaratorTail(std::shared_ptr<Type> &base)
    {
        // trailing [N]s — rightmost is innermost, so collect then reverse-wrap
        std::vector<size_t> dims;
        while (peek() == LEFT_BRACKET)
        {
            consume();
            Token sz = expect(CONSTANT);
            expect(RIGHT_BRACKET);
            dims.push_back(std::stoul(sz.lexeme));
        }
        for (const auto &dim : std::views::reverse(dims))
        {
            base = std::make_shared<ArrayType>(base, dim);
        }
    }

    Declarator parseDeclarator(std::shared_ptr<Type> base)
    {
        parseDeclaratorHead(base);
        const Token id = expect(IDENTIFIER);
        parseDeclaratorTail(base);
        return Declarator{base, id.lexeme, id.line, id.col};
    }

    std::shared_ptr<Type> parseAbstractDeclarator(std::shared_ptr<Type> base)
    {
        parseDeclaratorHead(base);
        parseDeclaratorTail(base);
        return base;
    }

    // ============================================================
    // Expression parsing — top-down (entry first, leaves last)
    // ============================================================

    std::shared_ptr<Expression> parseExpression()
    {
        std::shared_ptr<Expression> left = parseAssignment();
        while (peek() == COMMA)
        {
            Token op = consume();
            std::shared_ptr<Expression> right = parseAssignment();
            left = std::make_shared<BinaryExpr>(left->getLine(), left->getCol(), left, right,
                                                op.lexeme);
        }
        return left;
    }

    std::shared_ptr<Expression> parseAssignment()
    {
        std::shared_ptr<Expression> left = parseTernaryExpression();
        while (isAssignmentOp(peek()))
        {
            Token op = consume();
            std::shared_ptr<Expression> right = parseAssignment();
            left = std::make_shared<AssignExpr>(left, right, op.lexeme, left->getLine(),
                                                left->getCol());
        }
        return left;
    }

    std::shared_ptr<Expression> parseTernaryExpression()
    {
        std::shared_ptr<Expression> condition = parseBinaryExpression();
        if (peek() == QUESTION_MARK)
        {
            consume();
            std::shared_ptr<Expression> thenBranch = parseExpression();
            expect(COLON);
            std::shared_ptr<Expression> elseBranch = parseTernaryExpression();
            return std::make_shared<TernaryExpr>(condition, thenBranch, elseBranch,
                                                 condition->getLine(), condition->getCol());
        }
        return condition;
    }

    std::shared_ptr<Expression> parseBinaryExpression(int minPrecedence = 0)
    {

        std::shared_ptr<Expression> left = parseUnary();
        while (isBinaryOp(peek()) && precedenceLevel[peek()] >= minPrecedence)
        {
            Token op = consume();
            std::shared_ptr<Expression> right = parseBinaryExpression(precedenceLevel[op.type] + 1);
            left = std::make_shared<BinaryExpr>(left->getLine(), left->getCol(), left, right,
                                                op.lexeme);
        }
        return left;
    }

    std::shared_ptr<Expression> parseUnary()
    {
        if (isUnaryOp(peek()))
        {
            Token op = consume();
            std::shared_ptr<Expression> operand = parseUnary();
            return std::make_shared<UnaryExpr>(op.line, op.col, op.lexeme, operand);
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
                return std::make_shared<SizeOfExpr>(t.line, t.col, base, nullptr);
            }
            std::shared_ptr<Expression> expr = parseUnary();
            return std::make_shared<SizeOfExpr>(expr->getLine(), expr->getCol(), nullptr, expr);
        }
        if (peek() == LEFT_PAREN && isTypeStart(peekNext()))
        {
            consume();
            auto [base, line, col] = parseBaseType();
            base = parseAbstractDeclarator(base);
            expect(RIGHT_PAREN);
            auto operand = parseUnary();
            return std::make_shared<CastExpr>(base, operand, line, col);
        }
        std::shared_ptr<Expression> operand = parseFactor();
        return operand;
    }

    std::shared_ptr<Expression> parseFactor()
    {
        std::shared_ptr<Expression> node;
        if (peek() == CONSTANT)
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
            {
                throw std::logic_error("Number too big to be stored in 64 bits. line:" +
                                       std::to_string(constant.line) +
                                       " col:" + std::to_string(constant.col));
            }
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
            node = std::make_shared<IntLiterals>(constant.line, constant.col, value, constantType);
        }
        else if (peek() == LEFT_PAREN)
        {
            consume();
            std::shared_ptr<Expression> parseResult = parseExpression();
            expect(RIGHT_PAREN);
            node = parseResult;
        }
        else if (peek() == IDENTIFIER)
        {
            Token name = consume();
            node = std::make_shared<VariableExpr>(name.line, name.col, name.lexeme);
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
            node = std::make_shared<StringLiterals>(s.line, s.col, combined);
        }
        else
            throw std::logic_error("Invalid token as a factor " +
                                   std::string(tokenTypeToString(peek())));

        while (true)
        {
            if (peek() == LEFT_PAREN)
            {
                auto var = std::dynamic_pointer_cast<VariableExpr>(node);
                if (!var)
                    throw std::logic_error("callee must be an identifier");
                node = parseFunctionCallExpr(var);
            }
            else if (peek() == INC_OP || peek() == DEC_OP)
            {
                Token op = consume();
                node = std::make_shared<UnaryExpr>(node->getLine(), node->getCol(), op.lexeme, node,
                                                   true);
            }
            else if (peek() == LEFT_BRACKET)
            {
                consume();
                std::shared_ptr<Expression> index = parseExpression();
                expect(RIGHT_BRACKET);
                node =
                    std::make_shared<SubscriptExpr>(node, index, node->getLine(), node->getCol());
            }
            else if (peek() == DOT || peek() == PTR_OP)
            {
                Token op = consume();
                Token field = expect(IDENTIFIER);
                node = std::make_shared<MemberExpr>(node, field.lexeme, op.type == PTR_OP,
                                                    node->getLine(), node->getCol());
            }
            else
                break;
        }

        return node;
    }

    std::shared_ptr<FunctionCallExpr>
    parseFunctionCallExpr(std::shared_ptr<VariableExpr> functionName)
    {
        expect(LEFT_PAREN);
        std::vector<std::shared_ptr<Expression>> parameters;
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
        return std::make_shared<FunctionCallExpr>(functionName->getLine(), functionName->getCol(),
                                                  functionName, parameters);
    }

    std::shared_ptr<Expression> parseInitializers()
    {
        if (peek() == LEFT_BRACE)
        {
            Token brace = consume();
            if (peek() == RIGHT_BRACE)
            {
                throw std::logic_error("Empty Initializations are disallowed");
            }
            std::vector<std::shared_ptr<Expression>> initializations;
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
            return std::make_shared<InitExpr>(initializations, brace.line, brace.col);
        }

        return parseAssignment();
    }

    // ============================================================
    // Statement parsing — declarations, control flow, and blocks
    // ============================================================

    std::shared_ptr<ExprStmt> parseExprStatement(bool semiColon = true)
    {
        std::shared_ptr<Expression> expr = parseExpression();
        if (semiColon)
            expect(SEMI_COLON);
        return std::make_shared<ExprStmt>(expr, expr->getLine(), expr->getCol(), semiColon);
    }

    std::vector<std::shared_ptr<VarDecl>>
    parseVarDecl(const std::shared_ptr<Type> &type, bool global = false,
                 std::optional<StorageClass> storageClass = std::nullopt)
    {
        std::vector<std::shared_ptr<VarDecl>> variables;
        while (true)
        {
            auto [finalType, name, variable_line, variable_col] = parseDeclarator(type);
            std::shared_ptr<Expression> initialization;
            if (peek() == ASSIGN)
            {
                consume();
                initialization = parseInitializers();
            }
            variables.emplace_back(std::make_shared<VarDecl>(variable_line, variable_col, name,
                                                             finalType, initialization, global,
                                                             storageClass));
            if (peek() == COMMA)
            {
                consume();
                continue;
            }
            break;
        }
        expect(SEMI_COLON);
        return variables;
    }

    std::shared_ptr<DeclareStmt>
    parseDeclareStmt(std::shared_ptr<Type> &type, int line, int col,
                     std::optional<StorageClass> storageClass = std::nullopt)
    {
        if (peek() == LEFT_BRACE)
        {
            auto structVar = parseStructDecl(type, line, col);
            return std::make_shared<DeclareStmt>(line, col, structVar);
        }
        auto variables = parseVarDecl(type, false, storageClass);
        return std::make_shared<DeclareStmt>(line, col, variables);
    }

    std::shared_ptr<ReturnStmt> parseReturnStmt()
    {
        Token returnStart = expect(RETURN); // should not throw
        std::shared_ptr<Expression> returnExpression;
        if (peek() != SEMI_COLON)
            returnExpression = parseExpression();
        expect(SEMI_COLON);
        return std::make_shared<ReturnStmt>(returnStart.line, returnStart.col, returnExpression);
    }

    std::shared_ptr<IfStmt> parseIfStmt()
    {
        Token ifToken = expect(IF); // should never throw;
        expect(LEFT_PAREN);
        std::shared_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        if (peek() != LEFT_BRACE)
        {
            throw std::logic_error("if statement expects braces");
        }
        std::shared_ptr<BlockStmt> thenBlock = parseBlockStmt();

        std::shared_ptr<BlockStmt> elseBlock;
        if (peek() == ELSE)
        {
            consume();
            if (peek() == IF)
            {
                // `else if ...` is parsed as `else { if ... }`: parse the nested if
                // and wrap it in a synthetic block so it fits elseBlock's type.
                std::vector<std::shared_ptr<Statement>> body = {parseIfStmt()};
                elseBlock = std::make_shared<BlockStmt>(ifToken.line, ifToken.col, std::move(body));
            }
            else if (peek() == LEFT_BRACE)
            {
                elseBlock = parseBlockStmt();
            }
            else
            {
                throw std::logic_error("else statement expects '{' or 'if'");
            }
        }
        return std::make_shared<IfStmt>(ifToken.line, ifToken.col, condition, thenBlock, elseBlock);
    }

    std::shared_ptr<WhileStmt> parseWhileStmt()
    {
        Token whileToken = expect(WHILE);
        expect(LEFT_PAREN);
        std::shared_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        if (peek() != LEFT_BRACE)
        {
            throw std::logic_error("while statement expects braces");
        }
        std::shared_ptr<BlockStmt> whileBlock = parseBlockStmt();

        return std::make_shared<WhileStmt>(whileToken.line, whileToken.col, condition, whileBlock);
    }

    std::shared_ptr<DoWhileStmt> parseDoWhileStmt()
    {
        Token doStart = expect(DO); // should not throw
        if (peek() != LEFT_BRACE)
        {
            throw std::logic_error("do while statement expects braces");
        }
        std::shared_ptr<BlockStmt> doBlock = parseBlockStmt();
        expect(WHILE);
        expect(LEFT_PAREN);
        std::shared_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        expect(SEMI_COLON);
        return std::make_shared<DoWhileStmt>(doBlock, condition, doStart.line, doStart.col);
    }

    std::shared_ptr<ForStmt> parseForStmt()
    {
        Token forStart = expect(FOR);
        expect(LEFT_PAREN);
        std::shared_ptr<Statement> initialization;
        if (peek() == SEMI_COLON)
            consume();
        else if (isDeclSpecifierStart(peek()))
        {
            auto [type, storageClass, line, col] = parseDeclSpecifiers();
            // C99 6.8.5p3: a for-loop initializer declaration may only have auto
            // or register storage — static/extern are not allowed here.
            if (storageClass.has_value())
                throw std::logic_error(
                    "a storage-class specifier is not allowed in a for-loop initializer at line " +
                    std::to_string(line) + ", col " + std::to_string(col));
            initialization = parseDeclareStmt(type, line, col);
        }
        else
            initialization = parseExprStatement(true);
        std::shared_ptr<Statement> condition;
        if (peek() == SEMI_COLON)
            consume();
        else
            condition = parseExprStatement(true);
        std::shared_ptr<Statement> update;
        if (peek() != RIGHT_PAREN)
            update = parseExprStatement(false);
        expect(RIGHT_PAREN);
        if (peek() != LEFT_BRACE)
        {
            throw std::logic_error("for statement expects braces");
        }
        std::shared_ptr<BlockStmt> forBlock = parseBlockStmt();
        return std::make_shared<ForStmt>(forStart.line, forStart.col, initialization, condition,
                                         update, forBlock);
    }

    std::shared_ptr<BreakStmt> parseBreakStmt()
    {
        Token breakToken = expect(BREAK);
        expect(SEMI_COLON);
        return std::make_shared<BreakStmt>(breakToken.line, breakToken.col);
    }

    std::shared_ptr<ContinueStmt> parseContinueStmt()
    {
        Token continueToken = expect(CONTINUE);
        expect(SEMI_COLON);
        return std::make_shared<ContinueStmt>(continueToken.line, continueToken.col);
    }

    std::shared_ptr<BlockStmt> parseBlockStmt()
    {
        Token blockStart = expect(LEFT_BRACE); // should never throw
        std::vector<std::shared_ptr<Statement>> statements;
        while (peek() != RIGHT_BRACE)
        {
            if (isDeclSpecifierStart(peek()))
            {
                auto [type, storageClass, line, col] = parseDeclSpecifiers();
                if (peek() == IDENTIFIER && peekNext() == LEFT_PAREN)
                {
                    // Local function declaration: a forward prototype scoped to
                    // this block. Parse it through the same path as file scope and
                    // keep the node so SA can register it (and drop it at block exit).
                    Token identifier = expect(IDENTIFIER); // function name
                    consume();                             // (
                    auto decl = parseFunction(type, line, col, identifier, storageClass);
                    if (decl->statements)
                    {
                        throw std::logic_error(
                            "function definition not allowed in block scope at line " +
                            std::to_string(line) + ", col " + std::to_string(col));
                    }
                    statements.emplace_back(std::make_shared<FunctionDeclStmt>(line, col, decl));
                    continue;
                }
                statements.emplace_back(parseDeclareStmt(type, line, col, storageClass));
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
        consume(); // consume the right brace
        return std::make_shared<BlockStmt>(blockStart.line, blockStart.col, statements);
    }

    // ============================================================
    // Top-level / program — parameters, functions, structs, entry
    // ============================================================

    void parseParam(std::vector<Parameter> &parameters)
    {
        auto [type, baseLine, baseCol] = parseBaseType();
        parseDeclaratorHead(type);
        std::string name;
        int line = baseLine, col = baseCol;
        if (peek() == IDENTIFIER)
        {
            Token id = consume();
            name = id.lexeme;
            line = id.line;
            col = id.col;
        }
        parseDeclaratorTail(type);
        parameters.emplace_back(type, name, line, col);
    }

    std::shared_ptr<Function> parseFunction(std::shared_ptr<Type> returnType, int line, int col,
                                            Token functionName,
                                            std::optional<StorageClass> storageClass = std::nullopt)
    {
        std::string functionNameString = functionName.lexeme;
        std::vector<Parameter> parameters;
        bool variadic = false;

        if (peek() != RIGHT_PAREN && !(peek() == VOID && peekNext() == RIGHT_PAREN) &&
            peek() != ELLIPSIS)
        {
            parseParam(parameters);
            while (peek() == COMMA)
            {
                consume(); // comma
                if (peek() == ELLIPSIS)
                    break;
                parseParam(parameters);
            }
        }
        if (peek() == VOID && peekNext() == RIGHT_PAREN)
            consume();
        if (peek() == ELLIPSIS)
        {
            if (parameters.empty())
            {
                throw std::logic_error("There must be at least 1 named parameter. line: " +
                                       std::to_string(functionName.line) +
                                       ", col: " + std::to_string(functionName.col));
            }
            consume();
            variadic = true;
        }
        Token rightParen = expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> blockStmt;
        if (peek() == SEMI_COLON)
        {
            consume();
            return std::make_shared<Function>(line, col, functionNameString, returnType, parameters,
                                              blockStmt, variadic, storageClass);
        }
        if (peek() == LEFT_BRACE)
            blockStmt = parseBlockStmt();
        else
            expect(SEMI_COLON);

        return std::make_shared<Function>(line, col, functionNameString, returnType, parameters,
                                          blockStmt, variadic, storageClass);
    }

    std::shared_ptr<StructDecl> parseStructDecl(const std::shared_ptr<Type> &structType, int line,
                                                int col)
    {
        expect(LEFT_BRACE);
        if (peek() == RIGHT_BRACE)
        {
            throw std::logic_error("Empty struct body not allowed");
        }
        std::vector<StructField> fields;
        while (peek() != RIGHT_BRACE && peek() != EOF_TOKEN)
        {
            auto [type, line, col] = parseBaseType();
            while (peek() != SEMI_COLON && peek() != EOF_TOKEN)
            {
                parseDeclaratorHead(type);
                const Token id = expect(IDENTIFIER);
                parseDeclaratorTail(type);
                fields.emplace_back(type, id.lexeme, line, col);
                if (peek() == COMMA)
                    consume();
            }
            expect(SEMI_COLON);
        }
        expect(RIGHT_BRACE);
        expect(SEMI_COLON);
        auto actualStructType = std::dynamic_pointer_cast<StructType>(structType);
        return std::make_shared<StructDecl>(actualStructType->getName(), fields, line, col,
                                            structType);
    }

    std::shared_ptr<Program> ParseProgram()
    {
        std::vector<std::shared_ptr<TopLevelNode>> nodes;
        while (peek() != EOF_TOKEN)
        {

            if (isDeclSpecifierStart(peek()))
            {
                auto [type, storageClass, line, col] = parseDeclSpecifiers();
                parseDeclaratorHead(type);
                if (peek() == IDENTIFIER)
                {
                    if (peekNext() == LEFT_PAREN) // handle functions
                    {
                        Token identifier = expect(IDENTIFIER);
                        consume();
                        nodes.emplace_back(
                            parseFunction(type, line, col, identifier, storageClass));
                    }
                    else // handle variable declarations
                    {
                        parseDeclaratorTail(type);
                        auto variables = parseVarDecl(type, true, storageClass);
                        for (const auto &var : variables)
                        {
                            nodes.emplace_back(var);
                        }
                    }
                }
                else if (peek() == LEFT_BRACE)
                {
                    nodes.emplace_back(parseStructDecl(type, line, col));
                }
                else
                {
                    throw std::logic_error("Expected identifier or '{' at file scope, got " +
                                           std::string(tokenTypeToString(peek())) + " at line " +
                                           std::to_string(tokens[cur_token].line) + ", col " +
                                           std::to_string(tokens[cur_token].col));
                }
            }
            else
            {
                throw std::logic_error(
                    "Unexpected token at file scope: " + std::string(tokenTypeToString(peek())) +
                    " at line " + std::to_string(tokens[cur_token].line) + ", col " +
                    std::to_string(tokens[cur_token].col));
            }
        }
        return std::make_shared<Program>(nodes);
    }
};