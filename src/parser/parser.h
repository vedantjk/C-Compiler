#pragma once

#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

#include "../lexer/token.h"
#include "../ast/ASTNodes/Program.h"
#include "../ast/TopLevelNodes/Function.h"
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
            } else throw std::logic_error("Got invalid token in declare stmt: " + std::string(tokenTypeToString(peek())));
        }
        expect(SEMI_COLON);
        return std::make_shared<DeclareStmt>(type.line, type.col, variables);
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
            Token type = consume();
            initialization = parseDeclareStmt(type);
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
            else if(peek() == INT || peek() == CHAR){
                Token type = consume();
                statements.emplace_back(parseDeclareStmt(type));
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

    void parseParam(std::vector<Parameter>& parameters){
        if((peek() == INT || peek() == CHAR) && peekNext() == IDENTIFIER){
            Token type = consume(); // int, char
            Token param = consume();
            parameters.emplace_back(type.lexeme, param.lexeme, param.line, param.col);
        } else throw std::logic_error("Unexpected token in function parameters " + std::string{tokenTypeToString(peek())});
    }

    std::shared_ptr<Function> parseFunction(Token returnType, Token functionName){
        int line = returnType.line;
        int col = returnType.col;
        std::string returnTypeString = returnType.lexeme;
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