#ifndef LEXER_H
#define LEXER_H

#include "token.h"
#include "../utils/diagnostic.h"
#include <string>
#include <vector>
#include <iostream>
#include <cctype>
#include <unordered_map>

  class Lexer{
    std::string input;
    std::vector<Token> tokens;
    Diagnostic::DiagnosticEngine& diagnosticEngine;

    int start = 0;
    int current = 0;
    int line = 1;
    int col = 1;

    static std::unordered_map<std::string, TokenType> keywords;

    public:
    Lexer(std::string input, Diagnostic::DiagnosticEngine& diagnosticEngine) : input(std::move(input)), diagnosticEngine(diagnosticEngine) {}

    // ============================================================
    // Utility helpers — cursor advance, peek, match, error reporting
    // ============================================================

    void addToken(TokenType token){
        std::string lexeme = input.substr(start, current - start);
        if(token == EOF_TOKEN) lexeme = "";
        tokens.push_back({token, lexeme, line, col});
    }

    bool isAtEnd() const{
        return current >= (int)input.size();
    }

    char advance(){
        char c = input[current++];
        if(c == '\n') { line++; col = 1;}
        else if(c == '\t') { col += 4;}
        else col++;
        return c;
    }

    void error(int line, int col, std::string error_message){
        Diagnostic::DiagLevel diagLevel{Diagnostic::DiagLevel::LEXER};
        Diagnostic::Location location{line, col};
        diagnosticEngine.report(diagLevel, location, error_message);
    }

    bool match(char expected){
        if(isAtEnd()) return false;
        if(input[current]!=expected) return false;

        current++;
        return true;
    }

    char peek() const{
        if(isAtEnd()) return '\0';
        return input[current];
    }

    char peekNext() const{
        if (current + 1 >= (int)input.size()) return '\0';
        return input[current + 1];
    }

    // ============================================================
    // Token scanners — invoked once the leading char(s) identify the token kind
    // ============================================================

    void string(){
        while(peek() != '"' && !isAtEnd()){
            if(peek() == '\\') advance();
            advance();
        }

        if(isAtEnd()){
            error(line, col, "Unterminated string");
            return;
        }

        advance();
        addToken(STRING_LITERAL);
    }

    void number(){
    // Hex: 0x or 0X
    if (input[start] == '0' && (peek() == 'x' || peek() == 'X')) {
        advance(); // consume x/X
        if (!std::isxdigit(peek())) {
            error(line, col, "Hex constant requires at least one digit");
            return;
        }
        while (std::isxdigit(peek())) advance();
        consumeIntSuffix();
        addToken(CONSTANT);
        return;
    }

    while (std::isdigit(peek())) advance();

    if (peek() == '.' ) {
        advance();
        while(std::isdigit(peek())) advance();
        if(peek() == 'e' || peek() == 'E'){
            advance();
            if(peek() == '+' || peek() == '-') advance();
            if(!std::isdigit(peek())){
                error(line, col, "Exponent requires at least one digit");
                return;
            }
            while(std::isdigit(peek())) advance();
        }
        consumeFloatSuffix();
        addToken(CONSTANT);
        return;
    }

    if(peek() == 'e' || peek() == 'E'){
        advance();
        if(peek() == '+' || peek() == '-') advance();
        if(!std::isdigit(peek())){
            error(line, col, "Exponent requires at least one digit");
            return;
        }
        while(std::isdigit(peek())) advance();
        consumeFloatSuffix();
        addToken(CONSTANT);
        return;
    }

    consumeIntSuffix();
    addToken(CONSTANT);
   }

   void consumeFloatSuffix(){
        if (peek() == 'f' || peek() == 'F' ||
                    peek() == 'l' || peek() == 'L')
                advance();
   }

    void consumeIntSuffix(){
        // C89 IS: (u|U)(l|L)?|(l|L)(u|U)?
        if (peek() == 'u' || peek() == 'U') {
            advance();
            if (peek() == 'l' || peek() == 'L') advance();
        } else if (peek() == 'l' || peek() == 'L') {
            advance();
            if (peek() == 'u' || peek() == 'U') advance();
        }
    }

    void identifier(){
        while(std::isalnum(peek()) || peek() == '_') advance();

        std::string text = input.substr(start, current-start);
        TokenType type;
        auto it = keywords.find(text);
        if(it == keywords.end()) type = IDENTIFIER;
        else type = it->second;
        addToken(type);
    }

    void Char(){
        if(isAtEnd()){ error(line, col, "Unterminated character constant"); return; }

        // Empty character constant ''
        if(peek() == '\''){
            advance(); // consume closing quote
            error(line, col, "Empty character constant");
            return;
        }

        if(peek() == '\\'){
            advance(); // consume backslash
            if(isAtEnd()){ error(line, col, "Unterminated character constant"); return; }
            advance(); // consume escape char
        } else {
            advance(); // consume the character
        }

        if(isAtEnd() || peek() != '\''){
            error(line, col, "Unterminated or malformed character constant");
            // advance to closing quote or EOF for error recovery
            while(!isAtEnd() && peek() != '\'') advance();
            if(!isAtEnd()) advance();
            return;
        }

        advance(); // consume closing quote
        addToken(CONSTANT);
    }

    void comment(){
        advance();
        while(!isAtEnd() && !(peek() == '*' && peekNext() == '/')){
            advance();
        }

        if(isAtEnd()){
            error(line, col, "Unterminated block comment");
            return;
        }

        advance();
        advance();
    }

    // ============================================================
    // Main dispatch — branches on the next char to pick a scanner
    // ============================================================

    void scanToken(){
        char c = advance();
        switch(c){
            case '[': addToken(LEFT_BRACKET); break;
            case ']': addToken(RIGHT_BRACKET); break;
            case '~': addToken(TILDE); break;
            case '&':
                if(match('&')){
                    addToken(AND_OP);
                }
                else if(match('=')){
                    addToken(AND_ASSIGN);
                }
                else{
                    addToken(AMPERSAND);
                }
                break;
            case '|':
                if(match('=')) addToken(OR_ASSIGN);
                else if(match('|')) addToken(OR_OP);
                else addToken(PIPE);
                break;
            case '^': addToken(match('=')? XOR_ASSIGN : CARET); break;
            case '%': addToken(match('=') ? MOD_ASSIGN: PERCENT); break;
            case '?': addToken(QUESTION_MARK); break;
            case ':': addToken(COLON); break;
            case '(': addToken(LEFT_PAREN); break;
            case ')': addToken(RIGHT_PAREN); break;
            case '{': addToken(LEFT_BRACE); break;
            case '}': addToken(RIGHT_BRACE); break;
            case ',': addToken(COMMA); break;
            case '.':
                if(peek() == '.' && peekNext() == '.'){
                    advance();
                    advance();
                    addToken(ELLIPSIS);
                }else if(std::isdigit(peek())){
                    number();
                }else{
                    addToken(DOT);
                }
                break;
            case '-':
                if(match('-')){
                    addToken(DEC_OP);
                }
                else if(match('>')){
                    addToken(PTR_OP);
                }
                else if(match('=')){
                    addToken(SUB_ASSIGN);
                }
                else{
                    addToken(MINUS);
                }
                break;
            case '+':
                if(match('+')){
                    addToken(INC_OP);
                }else if(match('=')){
                    addToken(ADD_ASSIGN);
                }
                else{
                    addToken(PLUS);
                }
                break;
            case ';': addToken(SEMI_COLON); break;
            case '*': addToken(match('=') ? MUL_ASSIGN : ASTERISK); break;
            case '!':
                addToken(match('=') ? NE_OP : EXCLAMATION);
                break;
            case '=':
                addToken(match('=') ? EQ_OP : ASSIGN);
                break;
            case '<':
                if(match('<')){
                    match('=')? addToken(LEFT_ASSIGN) : addToken(LEFT_OP);
                }
                else if(match('=')){
                    addToken(LE_OP);
                }
                else{
                    addToken(LESS_THAN);
                }
                break;
            case '>':
                if(match('>')){
                    match('=') ? addToken(RIGHT_ASSIGN) : addToken(RIGHT_OP);
                }
                else if(match('=')){
                    addToken(GE_OP);
                }
                else{
                    addToken(GREATER_THAN);
                }
                break;
            case '/':
                if(match('=')){
                    addToken(DIV_ASSIGN);
                }
                else if (match('/')) {
                    while (peek() != '\n' && !isAtEnd()) advance();
                } else if(peek() == '*'){
                    comment();
                }else {
                    addToken(SLASH);
                }
                break;
                  case ' ':
            case '\r':
            case '\t':
                // Ignore whitespace.
                break;

            case '\n':
                break;
            case '"': string(); break;
            case '\'': Char(); break;
            default:
                if(std::isdigit(c)){
                    number();
                }else if(c == '_' || std::isalnum(c)){
                    identifier();
                }else{
                    error(line, col, "Unexpected character");
                }
                break;
        }
    }

    // ============================================================
    // Driver and output
    // ============================================================

    std::vector<Token> generateTokens(){
        while(!isAtEnd()){
            start = current;
            scanToken();
        }

        addToken(EOF_TOKEN);
        return std::move(tokens);
    }
    void printTokens(){
        for(const Token& token : tokens){
            std::cout<<token.toString()<<std::endl;
        }
    }
  };

#endif
