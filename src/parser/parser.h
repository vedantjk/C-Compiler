#pragma once

#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../lexer/token.h"
#include "../ast/ASTNodes/Program.h"
#include "../ast/TopLevelNodes/Function.h"
#include "../ast/Statements/DeclareStmt.h"
#include "../ast/Statements/ReturnStmt.h"
#include "../ast/Expressions/IntLiterals.h"
#include "../ast/Expressions/BinaryExpr.h"
#include "../ast/Expressions/VariableExpr.h"

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
    
    std::shared_ptr<Expression> parseFactor(){
        if(peek() == CONSTANT){
            Token constant = consume();
            return std::make_shared<IntLiterals>(constant.line, constant.col, constant.lexeme);
        }
        else if(peek() == LEFT_PAREN){
            consume();
            std::shared_ptr<Expression> parseResult = parseExpression();
            consume();
            return parseResult; 
        }else{
            Token factor = consume();
            return std::make_shared<VariableExpr>(factor.line, factor.col, factor.lexeme);
        } 
    }
    
    bool isBinaryOp(TokenType type){
        return type == OR_OP || type == AND_OP || type == EQ_OP || type == NE_OP || type == LESS_THAN || type == GREATER_THAN
        || type == LE_OP || type == GE_OP || type == PLUS || type == MINUS || type == ASTERISK || type == SLASH;
    }

    std::shared_ptr<Expression> parseExpression(int minPrecedence = 0){
        
        std::shared_ptr<Expression> left = parseFactor();
        while(isBinaryOp(peek()) && precedenceLevel[peek()] >= minPrecedence){
            Token op = consume();
            std::shared_ptr<Expression> right = parseExpression(precedenceLevel[op.type]+1);
            left = std::make_shared<BinaryExpr>(left->getLine(), left->getCol(), left, right, op.lexeme);
        }
        return left;
    }

    std::shared_ptr<DeclareStmt> parseDeclareStmt(Token type){
        std::vector<std::shared_ptr<VarDecl>> variables;
        std::string typeString = type.lexeme;
        while(peek() != SEMI_COLON){
            if(peek() == IDENTIFIER){
                Token id = consume();
                std::string idString = id.lexeme;
                std::shared_ptr<Expression> initialization;
                if(peek() == ASSIGN){
                    consume();
                    initialization = parseExpression();
                }
                if(peek() == COMMA) consume();
                variables.emplace_back(std::make_shared<VarDecl>(id.line, id.col, idString, typeString, initialization));
            }
        }
        consume(); // semicolon
        return std::make_shared<DeclareStmt>(type.line, type.col, variables);
    }

    std::shared_ptr<ReturnStmt> parseReturnStmt(){
        Token returnStart = expect(RETURN); // should not throw
        std::shared_ptr<Expression> returnExpression;
        if(peek() != SEMI_COLON) returnExpression = parseExpression();
        consume(); // semi colon
        return std::make_shared<ReturnStmt>(returnStart.line, returnStart.col, returnExpression);
    }

    std::shared_ptr<BlockStmt> parseBlockStmt(){
        Token blockStart = expect(LEFT_BRACE); // should never throw
        std::vector<std::shared_ptr<Statement>> statements;
        while(peek()!=RIGHT_BRACE){
            if(peek() == LEFT_BRACE){
                statements.emplace_back(parseBlockStmt());
            }
            else if(peek() == INT || peek() == CHAR){
                Token type = consume();
                statements.emplace_back(parseDeclareStmt(type));
            }
            else if(peek() == RETURN){
                statements.emplace_back(parseReturnStmt());
            }
            else throw std::logic_error("Unexpected token in block " + std::string{tokenTypeToString(peek())}); 
        }
        consume(); // consume the right brace
        return std::make_shared<BlockStmt>(blockStart.line, blockStart.col, statements);
    }

    std::shared_ptr<Function> parseFunction(Token returnType, Token functionName){
        int line = returnType.line;
        int col = returnType.col;
        std::string returnTypeString = returnType.lexeme;
        std::string functionNameString = functionName.lexeme;
        std::vector<Parameter> parameters;

        // add parameter handling later
        Token rightParen = expect(RIGHT_PAREN);
        std::shared_ptr<BlockStmt> blockStmt;
        if(peek() == LEFT_BRACE)
            blockStmt = parseBlockStmt();
        else expect(SEMI_COLON);

        return std::make_shared<Function>(line, col, functionNameString, returnTypeString, parameters, blockStmt);
    }

    std::shared_ptr<Program> ParseProgram() { 
        std::vector<std::shared_ptr<Function>> functions;
        while(peek() != EOF_TOKEN){
            if(peek() == INT || peek() == CHAR){
                Token type = consume();
                Token identifier = expect(IDENTIFIER);
                if(peek() == LEFT_PAREN){
                    consume();
                    functions.emplace_back(parseFunction(type, identifier));
                }
                // add varDecl support later
            } // add pointer support later
        }
        return std::make_shared<Program>(functions);
    }

};