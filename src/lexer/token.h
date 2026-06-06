#ifndef TOKEN_H
#define TOKEN_H

#include <string>

#define TOKEN_LIST                                                                                 \
    X(AUTO)                                                                                        \
    X(BREAK)                                                                                       \
    X(CASE)                                                                                        \
    X(CHAR)                                                                                        \
    X(CONST)                                                                                       \
    X(CONTINUE)                                                                                    \
    X(DEFAULT)                                                                                     \
    X(DO)                                                                                          \
    X(DOUBLE)                                                                                      \
    X(ELSE)                                                                                        \
    X(ENUM)                                                                                        \
    X(EXTERN)                                                                                      \
    X(FLOAT)                                                                                       \
    X(FOR)                                                                                         \
    X(GOTO)                                                                                        \
    X(IF)                                                                                          \
    X(INT)                                                                                         \
    X(LONG)                                                                                        \
    X(REGISTER)                                                                                    \
    X(RETURN)                                                                                      \
    X(SHORT)                                                                                       \
    X(SIGNED)                                                                                      \
    X(SIZEOF)                                                                                      \
    X(STATIC)                                                                                      \
    X(STRUCT)                                                                                      \
    X(SWITCH)                                                                                      \
    X(TYPEDEF)                                                                                     \
    X(UNION)                                                                                       \
    X(UNSIGNED)                                                                                    \
    X(VOID)                                                                                        \
    X(VOLATILE)                                                                                    \
    X(WHILE)                                                                                       \
    X(IDENTIFIER)                                                                                  \
    X(CONSTANT)                                                                                    \
    X(FLOATING_CONSTANT)                                                                           \
    X(STRING_LITERAL)                                                                              \
    X(RIGHT_ASSIGN)                                                                                \
    X(LEFT_ASSIGN)                                                                                 \
    X(ADD_ASSIGN)                                                                                  \
    X(SUB_ASSIGN)                                                                                  \
    X(MUL_ASSIGN)                                                                                  \
    X(DIV_ASSIGN)                                                                                  \
    X(MOD_ASSIGN)                                                                                  \
    X(AND_ASSIGN)                                                                                  \
    X(XOR_ASSIGN)                                                                                  \
    X(OR_ASSIGN)                                                                                   \
    X(RIGHT_OP)                                                                                    \
    X(LEFT_OP)                                                                                     \
    X(INC_OP)                                                                                      \
    X(DEC_OP)                                                                                      \
    X(PTR_OP)                                                                                      \
    X(AND_OP)                                                                                      \
    X(OR_OP)                                                                                       \
    X(LE_OP)                                                                                       \
    X(GE_OP)                                                                                       \
    X(EQ_OP)                                                                                       \
    X(NE_OP)                                                                                       \
    X(ELLIPSIS)                                                                                    \
    X(SEMI_COLON)                                                                                  \
    X(LEFT_BRACE)                                                                                  \
    X(RIGHT_BRACE)                                                                                 \
    X(COMMA)                                                                                       \
    X(COLON)                                                                                       \
    X(ASSIGN)                                                                                      \
    X(LEFT_PAREN)                                                                                  \
    X(RIGHT_PAREN)                                                                                 \
    X(LEFT_BRACKET)                                                                                \
    X(RIGHT_BRACKET)                                                                               \
    X(DOT)                                                                                         \
    X(AMPERSAND)                                                                                   \
    X(EXCLAMATION)                                                                                 \
    X(TILDE)                                                                                       \
    X(MINUS)                                                                                       \
    X(PLUS)                                                                                        \
    X(ASTERISK)                                                                                    \
    X(SLASH)                                                                                       \
    X(PERCENT)                                                                                     \
    X(LESS_THAN)                                                                                   \
    X(GREATER_THAN)                                                                                \
    X(CARET)                                                                                       \
    X(PIPE)                                                                                        \
    X(QUESTION_MARK)                                                                               \
    X(EOF_TOKEN)

enum TokenType
{
#define X(name) name,
    TOKEN_LIST
#undef X
};

inline const char *tokenTypeToString(TokenType t)
{
    static const char *names[] = {
#define X(name) #name,
        TOKEN_LIST
#undef X
    };
    return names[t];
}

class Token
{
  public:
    TokenType type;
    std::string lexeme;
    int line;
    int col;

    Token(TokenType type, std::string lexeme, int line, int col)
        : type(type), lexeme(lexeme), line(line), col(col)
    {
    }

    std::string toString() const
    {
        return "line: " + std::to_string(line) + " col: " + std::to_string(col) + " " +
               std::string(tokenTypeToString(type)) + " " + lexeme;
    }
};

#endif
