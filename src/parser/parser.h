#pragma once

#include <memory>
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
#include "../ast/Statements/IfStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/Statements/WhileStmt.h"
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
        return t == INT || t == CHAR || t == VOID || t == STRUCT;
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

    std::tuple<std::shared_ptr<Type>, int, int> parseBaseType()
    {
        if (peek() == INT)
        {
            auto t = consume();
            return {IntType::getInstance(), t.line, t.col};
        }
        if (peek() == CHAR)
        {
            auto t = consume();
            return {CharType::getInstance(), t.line, t.col};
        }
        if (peek() == VOID)
        {
            auto t = consume();
            return {VoidType::getInstance(), t.line, t.col};
        }
        if (peek() == STRUCT)
        {
            auto t = consume();
            Token name = expect(IDENTIFIER);
            return {std::make_shared<StructType>(name.lexeme), t.line, t.col};
        }
        throw std::logic_error("Expected type specifier, got " +
                               std::string(tokenTypeToString(peek())));
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
            node = std::make_shared<IntLiterals>(constant.line, constant.col, constant.lexeme);
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

    std::vector<std::shared_ptr<VarDecl>> parseVarDecl(const std::shared_ptr<Type> &type,
                                                       bool global = false)
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
                                                             finalType, initialization, global));
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

    std::shared_ptr<DeclareStmt> parseDeclareStmt(std::shared_ptr<Type> &type, int line, int col)
    {
        if (peek() == LEFT_BRACE)
        {
            auto structVar = parseStructDecl(type, line, col);
            return std::make_shared<DeclareStmt>(line, col, structVar);
        }
        auto variables = parseVarDecl(type);
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
        else if (isTypeStart(peek()))
        {
            auto [type, line, col] = parseBaseType();
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
        bool seenNonDeclStatements = false;
        while (peek() != RIGHT_BRACE)
        {
            if (isTypeStart(peek()))
            {
                if (seenNonDeclStatements)
                {
                    throw std::logic_error("cannot add declarations after statements.");
                }
                auto [type, line, col] = parseBaseType();
                if (peek() == IDENTIFIER && peekNext() == LEFT_PAREN)
                {
                    // local function declaration — parse and discard. C89 allows
                    // forward decls inside blocks; we lex the prototype away
                    // since the real definition lives at file scope.
                    consume(); // function name
                    consume(); // (
                    int depth = 1;
                    while (depth > 0 && peek() != EOF_TOKEN)
                    {
                        if (peek() == LEFT_PAREN)
                            depth++;
                        else if (peek() == RIGHT_PAREN)
                            depth--;
                        consume();
                    }
                    expect(SEMI_COLON);
                    continue;
                }
                statements.emplace_back(parseDeclareStmt(type, line, col));
                continue;
            }
            seenNonDeclStatements = true;
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
                                            Token functionName)
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
                                              blockStmt, variadic);
        }
        if (peek() == LEFT_BRACE)
            blockStmt = parseBlockStmt();
        else
            expect(SEMI_COLON);

        return std::make_shared<Function>(line, col, functionNameString, returnType, parameters,
                                          blockStmt, variadic);
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

            if (isTypeStart(peek()))
            {
                auto [type, line, col] = parseBaseType();
                parseDeclaratorHead(type);
                if (peek() == IDENTIFIER)
                {
                    if (peekNext() == LEFT_PAREN) // handle functions
                    {
                        Token identifier = expect(IDENTIFIER);
                        consume();
                        nodes.emplace_back(parseFunction(type, line, col, identifier));
                    }
                    else // handle variable declarations
                    {
                        parseDeclaratorTail(type);
                        auto variables = parseVarDecl(type, true);
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