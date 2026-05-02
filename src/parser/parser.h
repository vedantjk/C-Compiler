#pragma once

#include <memory>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../lexer/token.h"
#include "../ast/ASTNodes/Program.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../ast/TopLevelNodes/StructDecl.h"
#include "../ast/Statements/DeclareStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/Statements/IfStmt.h"
#include "../ast/Statements/WhileStmt.h"
#include "../ast/Statements/AssignStmt.h"
#include "../ast/Statements/ForStmt.h"
#include "../ast/Statements/FunctionCallStmt.h"
#include "../ast/Statements/BreakStmt.h"
#include "../ast/Statements/ContinueStmt.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Expressions/BinaryExpr.h"
#include "../ast/Expressions/VariableExpr.h"
#include "../ast/Expressions/FunctionCallExpr.h"
#include "../ast/Expressions/UnaryExpr.h"
#include "../ast/Expressions/StringLiterals.h"
#include "../ast/Expressions/SubscriptExpr.h"
#include "../ast/Expressions/MemberExpr.h"
#include "../ast/Expressions/SizeOfExpr.h"
#include "../ast/Expressions/CastExpr.h"
#include "../ast/Expressions/InitExpr.h"

struct Declarator {
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int col;
};

class Parser
{   
    std::unordered_map<TokenType, int> precedenceLevel = 
        {{OR_OP, 1}, {AND_OP, 2}, {EQ_OP, 3}, {NE_OP, 3},
        {LESS_THAN, 4}, {GREATER_THAN, 4}, {LE_OP, 4}, {GE_OP, 4},
        {PLUS, 5}, {MINUS, 5}, {ASTERISK, 6}, {SLASH, 6}};
    std::vector<Token> tokens;
    int cur_token = 0;
  public:
    Parser(std::vector<Token> tokens_) : tokens(std::move(tokens_)) {}

    TokenType peek()
    {
        if (cur_token == (int)tokens.size())
            return EOF_TOKEN;
        return tokens[cur_token].type;
    }

    TokenType peekNext(){
        if(cur_token + 1 >= (int) tokens.size()) return EOF_TOKEN;
        return tokens[cur_token+1].type;
    }

    Token consume() { 
        return tokens[cur_token++];
    }

    Token expect(TokenType type){
        if(peek() != type) throw std::logic_error("Unexpected token at line " + std::to_string(tokens[cur_token].line) + ", col " + std::to_string(tokens[cur_token].col) +
  ", expected " + tokenTypeToString(type) + ", received " + tokenTypeToString(tokens[cur_token].type));
        return consume();
    }

    static bool isTypeStart(const TokenType t) {
        return t == INT || t == CHAR || t == VOID || t == STRUCT;
    }

    std::tuple<std::shared_ptr<Type>, int, int> parseBaseType() {
        if (peek() == INT)  { auto t = consume(); return {IntType::getInstance(), t.line, t.col};  }
        if (peek() == CHAR) { auto t = consume(); return {CharType::getInstance(), t.line, t.col}; }
        if (peek() == VOID) { auto t = consume(); return {VoidType::getInstance(), t.line, t.col}; }
        if (peek() == STRUCT) {
            auto t = consume();
            Token name = expect(IDENTIFIER);
            return {std::make_shared<StructType>(name.lexeme), t.line, t.col};
        }
        throw std::logic_error(
            "Expected type specifier, got " + std::string(tokenTypeToString(peek())));
    }

    void parseDeclaratorHead(std::shared_ptr<Type>& base)
    {
        // leading *s — leftmost is outermost, so wrap in-order
        while (peek() == ASTERISK) {
            consume();
            base = std::make_shared<PointerType>(base);
        }

        if (peek() == LEFT_PAREN) {
            throw std::logic_error(
                "parenthesized declarators (e.g. int (*p)[10]) not supported yet");
        }
    }

    void parseDeclaratorTail(std::shared_ptr<Type>& base)
    {
        // trailing [N]s — rightmost is innermost, so collect then reverse-wrap
        std::vector<size_t> dims;
        while (peek() == LEFT_BRACKET) {
            consume();
            Token sz = expect(CONSTANT);
            expect(RIGHT_BRACKET);
            dims.push_back(std::stoul(sz.lexeme));
        }
        for (unsigned long long & dim : std::views::reverse(dims)) {
            base = std::make_shared<ArrayType>(base, dim);
        }
    }

    Declarator parseDeclarator(std::shared_ptr<Type> base) {
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

    std::shared_ptr<FunctionCallExpr> parseFunctionCallExpr(std::shared_ptr<VariableExpr> functionName){
        expect(LEFT_PAREN);
        std::vector<std::shared_ptr<Expression>> parameters;
        if (peek() != RIGHT_PAREN) {
            parameters.emplace_back(parseExpression());
            while (peek() == COMMA) {
                consume();
                parameters.emplace_back(parseExpression());
            }
        }
        expect(RIGHT_PAREN);
        return std::make_shared<FunctionCallExpr>(functionName->getLine(), functionName->getCol(), functionName, parameters);
    }

    std::shared_ptr<Expression> parseFactor(){
        std::shared_ptr<Expression> node;
        if(peek() == CONSTANT){
            Token constant = consume();
            node = std::make_shared<IntLiterals>(constant.line, constant.col, constant.lexeme);
        }
        else if(peek() == LEFT_PAREN){
            consume();
            std::shared_ptr<Expression> parseResult = parseExpression();
            expect(RIGHT_PAREN);
            node = parseResult; 
        }else if(peek() == IDENTIFIER){
            Token name = consume();
            node = std::make_shared<VariableExpr>(name.line, name.col, name.lexeme);
        }else if(peek() == STRING_LITERAL){
            Token s = consume();
            int len = s.lexeme.size();
            std::string combined = s.lexeme.substr(1, len - 2);
            while(peek() == STRING_LITERAL){
                Token r = consume();
                int newLen = r.lexeme.size();
                combined += r.lexeme.substr(1, newLen - 2);
            }
            combined = '"' + combined + '"';
            node = std::make_shared<StringLiterals>(s.line, s.col, combined);
        } else throw std::logic_error("Invalid token as a factor " + std::string(tokenTypeToString(peek())));

        while(true){
            if(peek() == LEFT_PAREN){
                auto var = std::dynamic_pointer_cast<VariableExpr>(node);
                if(!var) throw std::logic_error("callee must be an identifier");
                node = parseFunctionCallExpr(var);
            }else if(peek() == INC_OP || peek() == DEC_OP){
                Token op = consume();
                node = std::make_shared<UnaryExpr>(node->getLine(),node->getCol(), op.lexeme, node, true);
            }else if (peek() == LEFT_BRACKET){
                consume();
                std::shared_ptr<Expression> index = parseExpression();
                expect(RIGHT_BRACKET);
                node = std::make_shared<SubscriptExpr>(node, index, node->getLine(), node->getCol());
            }else if (peek() == DOT || peek() == PTR_OP){
                Token op = consume();
                Token field = expect(IDENTIFIER);
                node = std::make_shared<MemberExpr>(node, field.lexeme, op.type == PTR_OP, node->getLine(), node->getCol());
            }else break;
        }

        return node;
    }
    
    bool isBinaryOp(TokenType type){
        return type == OR_OP || type == AND_OP || type == EQ_OP || type == NE_OP || type == LESS_THAN || type == GREATER_THAN
        || type == LE_OP || type == GE_OP || type == PLUS || type == MINUS || type == ASTERISK || type == SLASH;
    }

    bool isUnaryOp(TokenType type){
        return type == EXCLAMATION || type == TILDE || type == MINUS || type == PLUS || type == AMPERSAND || type == ASTERISK || type == INC_OP || type == DEC_OP;
    }

    std::shared_ptr<Expression> parseUnary(){
        if(isUnaryOp(peek())){
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

    std::shared_ptr<Expression> parseExpression(int minPrecedence = 0){
        
        std::shared_ptr<Expression> left = parseUnary();
        while(isBinaryOp(peek()) && precedenceLevel[peek()] >= minPrecedence){
            Token op = consume();
            std::shared_ptr<Expression> right = parseExpression(precedenceLevel[op.type]+1);
            left = std::make_shared<BinaryExpr>(left->getLine(), left->getCol(), left, right, op.lexeme);
        }
        return left;
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
                    if (peek() == RIGHT_BRACE) break;
                    initializations.emplace_back(parseInitializers());
                }
            }
            expect(RIGHT_BRACE);
            return std::make_shared<InitExpr>(initializations, brace.line, brace.col);
        }

        return parseExpression();
    }

    std::vector<std::shared_ptr<VarDecl>> parseVarDecl(const std::shared_ptr<Type>& type, bool global = false)
    {
        std::vector<std::shared_ptr<VarDecl>> variables;
        while(peek() != SEMI_COLON && peek() != EOF_TOKEN){
            auto [finalType, name, variable_line, variable_col] = parseDeclarator(type);
            std::shared_ptr<Expression> initialization;
            if(peek() == ASSIGN){
                consume();
                initialization = parseInitializers();
            }
            if(peek() == COMMA) consume();
            variables.emplace_back(std::make_shared<VarDecl>(variable_line, variable_col, name, finalType, initialization, global));
        }
        expect(SEMI_COLON);
        return variables;
    }

    std::shared_ptr<DeclareStmt> parseDeclareStmt(const std::shared_ptr<Type>& type, int line, int col){
        if (peek() == LEFT_BRACE)
        {
            auto structVar = parseStructDecl(type, line, col);
            return std::make_shared<DeclareStmt>(line, col, structVar);
        }
        auto variables = parseVarDecl(type);
        return std::make_shared<DeclareStmt>(line, col, variables);
    }

    std::shared_ptr<ReturnStmt> parseReturnStmt(){
        Token returnStart = expect(RETURN); // should not throw
        std::shared_ptr<Expression> returnExpression;
        if(peek() != SEMI_COLON) returnExpression = parseExpression();
        expect(SEMI_COLON); 
        return std::make_shared<ReturnStmt>(returnStart.line, returnStart.col, returnExpression);
    }

    std::shared_ptr<WhileStmt> parseWhileStmt(){
        Token whileToken = expect(WHILE);
        expect(LEFT_PAREN);
        std::shared_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> whileBlock = parseBlockStmt();

        return std::make_shared<WhileStmt>(whileToken.line, whileToken.col, condition, whileBlock);
    }

    std::shared_ptr<IfStmt> parseIfStmt(){
        Token ifToken = expect(IF); // should never throw;
        expect(LEFT_PAREN); 
        std::shared_ptr<Expression> condition = parseExpression();
        expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> thenBlock = parseBlockStmt();

        std::shared_ptr<BlockStmt> elseBlock;
        if(peek() == ELSE){
            consume();
            elseBlock = parseBlockStmt();
        }
        return std::make_shared<IfStmt>(ifToken.line, ifToken.col, condition, thenBlock, elseBlock);
    }

    std::shared_ptr<AssignStmt> parseAssignStmt(bool semi_colon = true){
        std::shared_ptr<Expression> lhs = parseExpression();
        expect(ASSIGN); // for '='
        std::shared_ptr<Expression> rhs = parseExpression();
        if(semi_colon) expect(SEMI_COLON); 
        return std::make_shared<AssignStmt>(lhs->getLine(), lhs->getCol(), lhs, rhs);
    }

    std::shared_ptr<ForStmt> parseForStmt(){
        Token forStart = expect(FOR);
        expect(LEFT_PAREN);
        std::shared_ptr<Statement> initialization;
        if(peek() == INT || peek() == CHAR){
            auto [type, line, col] = parseBaseType();
            initialization = parseDeclareStmt(type, line, col);
        }else{
            initialization = parseAssignStmt();
        }
        std::shared_ptr<Expression> condition = parseExpression();
        expect(SEMI_COLON); 
        std::shared_ptr<Statement> update = parseAssignStmt(false);
        expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> forBlock = parseBlockStmt();
        return std::make_shared<ForStmt>(forStart.line, forStart.col, initialization, condition, update, forBlock);
    }

    std::shared_ptr<FunctionCallStmt> parseFunctionCallStmt(){
        Token functionName = expect(IDENTIFIER);
        expect(LEFT_PAREN);
        std::vector<std::shared_ptr<Expression>> parameters;
        if (peek() != RIGHT_PAREN) {
            parameters.emplace_back(parseExpression());
            while (peek() == COMMA) {
                consume();
                parameters.emplace_back(parseExpression());
            }
        }
        expect(RIGHT_PAREN);
        expect(SEMI_COLON);
        return std::make_shared<FunctionCallStmt>(functionName.line, functionName.col, functionName.lexeme, parameters);
    }

    std::shared_ptr<BreakStmt> parseBreakStmt(){
        Token breakToken = expect(BREAK);
        expect(SEMI_COLON);
        return std::make_shared<BreakStmt>(breakToken.line, breakToken.col);
    }

    std::shared_ptr<ContinueStmt> parseContinueStmt(){
        Token continueToken = expect(CONTINUE);
        expect(SEMI_COLON);
        return std::make_shared<ContinueStmt>(continueToken.line, continueToken.col);
    }

    std::shared_ptr<BlockStmt> parseBlockStmt(){
        Token blockStart = expect(LEFT_BRACE); // should never throw
        std::vector<std::shared_ptr<Statement>> statements;
        while(peek()!=RIGHT_BRACE){
            if(peek() == LEFT_BRACE){
                statements.emplace_back(parseBlockStmt());
            }
            else if (peek() == SEMI_COLON)
            {
                consume();
            }
            else if(isTypeStart(peek())){
                auto [type, line, col] = parseBaseType();
                statements.emplace_back(parseDeclareStmt(type, line, col));
            }
            else if(peek() == RETURN){
                statements.emplace_back(parseReturnStmt());
            }
            else if(peek() == IF){
                statements.emplace_back(parseIfStmt());
            }
            else if(peek() == WHILE){
                statements.emplace_back(parseWhileStmt());
            }
            else if(peek() == IDENTIFIER){
                if(peekNext() == LEFT_PAREN) statements.emplace_back(parseFunctionCallStmt());
                else statements.emplace_back(parseAssignStmt());
            }
            else if(peek() == FOR){
                statements.emplace_back(parseForStmt());
            }
            else if(peek() == BREAK){
                statements.emplace_back(parseBreakStmt());
            }
            else if(peek() == CONTINUE){
                statements.emplace_back(parseContinueStmt());
            }
            else throw std::logic_error("Unexpected token in block " + std::string{tokenTypeToString(peek())}); 
        }
        consume(); // consume the right brace
        return std::make_shared<BlockStmt>(blockStart.line, blockStart.col, statements);
    }

    void parseParam(std::vector<Parameter>& parameters)
    {
        const auto type = parseBaseType();
        auto finalType = parseDeclarator(std::get<0>(type));
        parameters.emplace_back(finalType.type, finalType.name, finalType.line, finalType.col);
    }

    std::shared_ptr<Function> parseFunction(std::shared_ptr<Type> returnType, int line, int col, Token functionName){
        std::string functionNameString = functionName.lexeme;
        std::vector<Parameter> parameters;

        if(peek()!=RIGHT_PAREN){
            parseParam(parameters);
            while(peek() == COMMA){
                consume(); // comma
                parseParam(parameters);
            }
        }
        
        Token rightParen = expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> blockStmt;
        if(peek() == LEFT_BRACE)
            blockStmt = parseBlockStmt();
        else expect(SEMI_COLON);

        return std::make_shared<Function>(line, col, functionNameString, returnType, parameters, blockStmt);
    }

    std::shared_ptr<StructDecl> parseStructDecl(const std::shared_ptr<Type>& structType, int line, int col)
    {
        expect(LEFT_BRACE);
        if (peek() == RIGHT_BRACE)
        {
            throw std::logic_error("Empty struct body not allowed");
        }
        std::vector<StructField> fields;
        while (peek()!=RIGHT_BRACE && peek()!=EOF_TOKEN){
            auto [type, line, col] = parseBaseType();
            while (peek()!=SEMI_COLON && peek()!=EOF_TOKEN)
            {
                parseDeclaratorHead(type);
                const Token id = expect(IDENTIFIER);
                parseDeclaratorTail(type);
                fields.emplace_back(type, id.lexeme, line, col);
                if (peek() == COMMA) consume();
            }
            expect(SEMI_COLON);
        }
        expect(RIGHT_BRACE);
        expect(SEMI_COLON);
        return std::make_shared<StructDecl>(structType->toString(), fields, line, col);
    }

    std::shared_ptr<Program> ParseProgram() { 
        std::vector<std::shared_ptr<TopLevelNode>> nodes;
        while(peek() != EOF_TOKEN){

            if(isTypeStart(peek())){
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
                        for (const auto& var : variables)
                        {
                            nodes.emplace_back(var);
                        }
                    }
                }else if (peek() == LEFT_BRACE)
                {
                    nodes.emplace_back(parseStructDecl(type, line, col));
                }
            } // add pointer support later

        }
        return std::make_shared<Program>(nodes);
    }

};