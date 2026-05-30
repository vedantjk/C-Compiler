// test_lexer.cpp - Comprehensive C89 lexer tests
//
// Tests all token types from the ANSI C (C89) grammar.
// Reference: https://www.lysator.liu.se/c/ANSI-C-grammar-l.html

#include "lexer.h"

#include <functional>
#include <sstream>

// ============================================================
// Test infrastructure
// ============================================================

static int g_passed = 0;
static int g_failed = 0;
static int g_total = 0;

// Run the lexer on input, return token vector directly.
std::vector<Token> tokenize(const std::string &input)
{
    // Suppress stderr during tokenization so lexer errors
    // don't pollute test output for non-error tests.
    std::streambuf *old_cerr = std::cerr.rdbuf();
    std::ostringstream suppress;
    std::cerr.rdbuf(suppress.rdbuf());

    Diagnostic::DiagnosticEngine diag;
    Lexer lexer(input, diag);
    auto tokens = lexer.generateTokens();

    std::cerr.rdbuf(old_cerr);
    return tokens;
}

// Run the lexer, returning both tokens and any error output (from stderr).
struct TokenizeResult
{
    std::vector<Token> tokens;
    std::string errors;
};

TokenizeResult tokenize_with_errors(const std::string &input)
{
    std::streambuf *old_cerr = std::cerr.rdbuf();
    std::ostringstream capture;
    std::cerr.rdbuf(capture.rdbuf());

    Diagnostic::DiagnosticEngine diag;
    Lexer lexer(input, diag);
    auto tokens = lexer.generateTokens();
    diag.print(); // flush diagnostics to captured stderr

    std::cerr.rdbuf(old_cerr);
    return {std::move(tokens), capture.str()};
}

void runTest(const std::string &name, std::function<bool()> fn)
{
    g_total++;
    bool ok = false;
    try
    {
        ok = fn();
    }
    catch (...)
    {
        ok = false;
    }
    if (ok)
    {
        g_passed++;
    }
    else
    {
        g_failed++;
        std::cerr << "  FAIL: " << name << std::endl;
    }
}

#define TEST(name, body) runTest(name, [&]() -> bool { body })

#define EXPECT(cond)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "    Expected: " #cond << std::endl;                                      \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

// Compare token at index i: constructs "TYPE lexeme" from Token fields.
#define EXPECT_TOKEN(toks, i, exp)                                                                 \
    do                                                                                             \
    {                                                                                              \
        if ((i) >= (int)(toks).size())                                                             \
        {                                                                                          \
            std::cerr << "    [" << (i) << "] OUT_OF_BOUNDS (size=" << (toks).size() << ")"        \
                      << std::endl;                                                                \
            return false;                                                                          \
        }                                                                                          \
        std::string _got =                                                                         \
            std::string(tokenTypeToString((toks)[(i)].type)) + " " + (toks)[(i)].lexeme;           \
        std::string _exp = (exp);                                                                  \
        if (_got != _exp)                                                                          \
        {                                                                                          \
            std::cerr << "    [" << (i) << "] expected: \"" << _exp << "\"  got: \"" << _got       \
                      << "\"" << std::endl;                                                        \
            return false;                                                                          \
        }                                                                                          \
    } while (0)

// ============================================================
// 1. Keywords  (all 32 C89 keywords)
// ============================================================
void test_keywords()
{
    std::cerr << "\n--- Keywords ---" << std::endl;
    struct
    {
        const char *src;
        const char *type;
    } kws[] = {
        {"auto", "AUTO"},         {"break", "BREAK"},
        {"case", "CASE"},         {"char", "CHAR"},
        {"const", "CONST"},       {"continue", "CONTINUE"},
        {"default", "DEFAULT"},   {"do", "DO"},
        {"double", "DOUBLE"},     {"else", "ELSE"},
        {"enum", "ENUM"},         {"extern", "EXTERN"},
        {"float", "FLOAT"},       {"for", "FOR"},
        {"goto", "GOTO"},         {"if", "IF"},
        {"int", "INT"},           {"long", "LONG"},
        {"register", "REGISTER"}, {"return", "RETURN"},
        {"short", "SHORT"},       {"signed", "SIGNED"},
        {"sizeof", "SIZEOF"},     {"static", "STATIC"},
        {"struct", "STRUCT"},     {"switch", "SWITCH"},
        {"typedef", "TYPEDEF"},   {"union", "UNION"},
        {"unsigned", "UNSIGNED"}, {"void", "VOID"},
        {"volatile", "VOLATILE"}, {"while", "WHILE"},
    };
    for (auto &kw : kws)
    {
        std::string name = std::string("keyword '") + kw.src + "'";
        std::string exp = std::string(kw.type) + " " + kw.src;
        TEST(name, {
            auto t = tokenize(kw.src);
            EXPECT_TOKEN(t, 0, exp);
            return true;
        });
    }
    // keyword-like identifiers must NOT match
    TEST("'integer' is identifier, not keyword", {
        auto t = tokenize("integer");
        EXPECT_TOKEN(t, 0, "IDENTIFIER integer");
        return true;
    });
    TEST("'returns' is identifier, not keyword", {
        auto t = tokenize("returns");
        EXPECT_TOKEN(t, 0, "IDENTIFIER returns");
        return true;
    });
}

// ============================================================
// 2. Identifiers
// ============================================================
void test_identifiers()
{
    std::cerr << "\n--- Identifiers ---" << std::endl;

    TEST("single letter", {
        auto t = tokenize("x");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        return true;
    });
    TEST("multi-letter", {
        auto t = tokenize("foo");
        EXPECT_TOKEN(t, 0, "IDENTIFIER foo");
        return true;
    });
    TEST("letters and digits", {
        auto t = tokenize("var1");
        EXPECT_TOKEN(t, 0, "IDENTIFIER var1");
        return true;
    });
    TEST("all caps", {
        auto t = tokenize("FOO");
        EXPECT_TOKEN(t, 0, "IDENTIFIER FOO");
        return true;
    });
    TEST("mixed case", {
        auto t = tokenize("myVar");
        EXPECT_TOKEN(t, 0, "IDENTIFIER myVar");
        return true;
    });
    // C89 requires underscore support: {L}({L}|{D})* where L = [a-zA-Z_]
    TEST("underscore in body", {
        auto t = tokenize("my_var");
        EXPECT_TOKEN(t, 0, "IDENTIFIER my_var");
        return true;
    });
    TEST("leading underscore", {
        auto t = tokenize("_foo");
        EXPECT_TOKEN(t, 0, "IDENTIFIER _foo");
        return true;
    });
    TEST("double leading underscore", {
        auto t = tokenize("__LINE__");
        EXPECT_TOKEN(t, 0, "IDENTIFIER __LINE__");
        return true;
    });
    TEST("only underscore", {
        auto t = tokenize("_");
        EXPECT_TOKEN(t, 0, "IDENTIFIER _");
        return true;
    });
    TEST("trailing digits", {
        auto t = tokenize("tmp42");
        EXPECT_TOKEN(t, 0, "IDENTIFIER tmp42");
        return true;
    });
}

// ============================================================
// 3. Integer constants
// ============================================================
void test_integer_constants()
{
    std::cerr << "\n--- Integer Constants ---" << std::endl;

    TEST("zero", {
        auto t = tokenize("0");
        EXPECT_TOKEN(t, 0, "CONSTANT 0");
        return true;
    });
    TEST("single digit", {
        auto t = tokenize("7");
        EXPECT_TOKEN(t, 0, "CONSTANT 7");
        return true;
    });
    TEST("multi-digit", {
        auto t = tokenize("42");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        return true;
    });
    TEST("large number", {
        auto t = tokenize("1234567890");
        EXPECT_TOKEN(t, 0, "CONSTANT 1234567890");
        return true;
    });
    // C89: 0[xX]{H}+{IS}?
    TEST("hex constant lower", {
        auto t = tokenize("0xff");
        EXPECT_TOKEN(t, 0, "CONSTANT 0xff");
        return true;
    });
    TEST("hex constant upper", {
        auto t = tokenize("0XFF");
        EXPECT_TOKEN(t, 0, "CONSTANT 0XFF");
        return true;
    });
    // C89: 0{D}+{IS}?
    TEST("octal constant", {
        auto t = tokenize("0777");
        EXPECT_TOKEN(t, 0, "CONSTANT 0777");
        return true;
    });
    // C89: integer suffixes u/U/l/L
    TEST("unsigned suffix u", {
        auto t = tokenize("42u");
        EXPECT_TOKEN(t, 0, "CONSTANT 42u");
        return true;
    });
    TEST("long suffix L", {
        auto t = tokenize("42L");
        EXPECT_TOKEN(t, 0, "CONSTANT 42L");
        return true;
    });
    TEST("unsigned long suffix UL", {
        auto t = tokenize("42UL");
        EXPECT_TOKEN(t, 0, "CONSTANT 42UL");
        return true;
    });
}

// ============================================================
// 4. Float constants
// ============================================================
void test_float_constants()
{
    std::cerr << "\n--- Float Constants ---" << std::endl;

    TEST("simple float", {
        auto t = tokenize("3.14");
        EXPECT_TOKEN(t, 0, "CONSTANT 3.14");
        return true;
    });
    TEST("float with leading zero", {
        auto t = tokenize("0.5");
        EXPECT_TOKEN(t, 0, "CONSTANT 0.5");
        return true;
    });
    // C89: {D}+{E}{FS}?
    TEST("float with exponent", {
        auto t = tokenize("1e10");
        EXPECT_TOKEN(t, 0, "CONSTANT 1e10");
        return true;
    });
    TEST("float with negative exponent", {
        auto t = tokenize("1e-5");
        EXPECT_TOKEN(t, 0, "CONSTANT 1e-5");
        return true;
    });
    // C89: {D}*"."{D}+({E})?{FS}?
    TEST("float starting with dot", {
        auto t = tokenize(".5");
        EXPECT_TOKEN(t, 0, "CONSTANT .5");
        return true;
    });
    // C89: {D}+"."{D}*({E})?{FS}?
    TEST("float with decimal and exponent", {
        auto t = tokenize("3.14e2");
        EXPECT_TOKEN(t, 0, "CONSTANT 3.14e2");
        return true;
    });
    // C89: float suffixes f/F/l/L
    TEST("float with f suffix", {
        auto t = tokenize("3.14f");
        EXPECT_TOKEN(t, 0, "CONSTANT 3.14f");
        return true;
    });
    TEST("float trailing dot", {
        auto t = tokenize("42.");
        EXPECT_TOKEN(t, 0, "CONSTANT 42.");
        return true;
    });
}

// ============================================================
// 5. Character constants
// ============================================================
void test_char_constants()
{
    std::cerr << "\n--- Character Constants ---" << std::endl;

    // C89: L?'(\\.|[^\\'])+'
    TEST("simple char 'a'", {
        auto t = tokenize("'a'");
        EXPECT_TOKEN(t, 0, "CONSTANT 'a'");
        return true;
    });
    TEST("char digit '0'", {
        auto t = tokenize("'0'");
        EXPECT_TOKEN(t, 0, "CONSTANT '0'");
        return true;
    });
    TEST("escaped newline", {
        auto t = tokenize("'\\n'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\n'");
        return true;
    });
    TEST("escaped backslash", {
        auto t = tokenize("'\\\\'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\\\'");
        return true;
    });
    TEST("escaped null", {
        auto t = tokenize("'\\0'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\0'");
        return true;
    });
    TEST("escaped single quote", {
        auto t = tokenize("'\\''");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\''");
        return true;
    });
    TEST("escaped tab", {
        auto t = tokenize("'\\t'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\t'");
        return true;
    });
    TEST("escaped carriage return", {
        auto t = tokenize("'\\r'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\r'");
        return true;
    });
    TEST("escaped alert/bell", {
        auto t = tokenize("'\\a'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\a'");
        return true;
    });
    TEST("escaped double-quote", {
        auto t = tokenize("'\\\"'");
        EXPECT_TOKEN(t, 0, "CONSTANT '\\\"'");
        return true;
    });
}

// ============================================================
// 6. String literals
// ============================================================
void test_string_literals()
{
    std::cerr << "\n--- String Literals ---" << std::endl;

    TEST("simple string", {
        auto t = tokenize("\"hello\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "hello")");
        return true;
    });
    TEST("empty string", {
        auto t = tokenize("\"\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "")");
        return true;
    });
    TEST("string with spaces", {
        auto t = tokenize("\"hello world\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "hello world")");
        return true;
    });
    TEST("string with escape sequence", {
        auto t = tokenize("\"hello\\nworld\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "hello\nworld")");
        return true;
    });
    TEST("string with tab escape", {
        auto t = tokenize("\"col1\\tcol2\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "col1\tcol2")");
        return true;
    });
    // C89: escaped quote inside string
    TEST("string with escaped quote", {
        auto t = tokenize("\"say \\\"hi\\\"\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "say \"hi\"")");
        return true;
    });
}

// ============================================================
// 7. Single-character punctuation & operators
// ============================================================
void test_single_char_tokens()
{
    std::cerr << "\n--- Single-char Tokens ---" << std::endl;

    struct
    {
        const char *src;
        const char *exp;
    } cases[] = {
        {"(", "LEFT_PAREN ("},   {")", "RIGHT_PAREN )"},   {"{", "LEFT_BRACE {"},
        {"}", "RIGHT_BRACE }"},  {",", "COMMA ,"},         {".", "DOT ."},
        {"-", "MINUS -"},        {"+", "PLUS +"},          {";", "SEMI_COLON ;"},
        {"*", "ASTERISK *"},     {"/", "SLASH /"},         {"!", "EXCLAMATION !"},
        {"=", "ASSIGN ="},       {"<", "LESS_THAN <"},     {">", "GREATER_THAN >"},
        {"[", "LEFT_BRACKET ["}, {"]", "RIGHT_BRACKET ]"}, {"~", "TILDE ~"},
        {"&", "AMPERSAND &"},    {"|", "PIPE |"},          {"^", "CARET ^"},
        {"%", "PERCENT %"},      {"?", "QUESTION_MARK ?"}, {":", "COLON :"},
    };
    for (auto &c : cases)
    {
        std::string name = std::string("'") + c.src + "'";
        TEST(name, {
            auto t = tokenize(c.src);
            EXPECT_TOKEN(t, 0, std::string(c.exp));
            return true;
        });
    }
}

// ============================================================
// 8. Two-character operators
// ============================================================
void test_two_char_operators()
{
    std::cerr << "\n--- Two-char Operators ---" << std::endl;

    struct
    {
        const char *src;
        const char *exp;
    } cases[] = {
        {"!=", "NE_OP !="},      {"==", "EQ_OP =="},      {"<=", "LE_OP <="},
        {">=", "GE_OP >="},      {"++", "INC_OP ++"},     {"--", "DEC_OP --"},
        {"->", "PTR_OP ->"},     {"&&", "AND_OP &&"},     {"||", "OR_OP ||"},
        {"<<", "LEFT_OP <<"},    {">>", "RIGHT_OP >>"},   {"+=", "ADD_ASSIGN +="},
        {"-=", "SUB_ASSIGN -="}, {"*=", "MUL_ASSIGN *="}, {"/=", "DIV_ASSIGN /="},
        {"%=", "MOD_ASSIGN %="}, {"&=", "AND_ASSIGN &="}, {"^=", "XOR_ASSIGN ^="},
        {"|=", "OR_ASSIGN |="},
    };
    for (auto &c : cases)
    {
        std::string name = std::string("'") + c.src + "'";
        TEST(name, {
            auto t = tokenize(c.src);
            EXPECT_TOKEN(t, 0, std::string(c.exp));
            return true;
        });
    }
}

// ============================================================
// 9. Three-character tokens
// ============================================================
void test_three_char_tokens()
{
    std::cerr << "\n--- Three-char Tokens ---" << std::endl;

    TEST("ellipsis '...'", {
        auto t = tokenize("...");
        EXPECT_TOKEN(t, 0, "ELLIPSIS ...");
        return true;
    });
    TEST("right shift assign '>>='", {
        auto t = tokenize(">>=");
        EXPECT_TOKEN(t, 0, "RIGHT_ASSIGN >>=");
        return true;
    });
    TEST("left shift assign '<<='", {
        auto t = tokenize("<<=");
        EXPECT_TOKEN(t, 0, "LEFT_ASSIGN <<=");
        return true;
    });
}

// ============================================================
// 10. Comments
// ============================================================
void test_comments()
{
    std::cerr << "\n--- Comments ---" << std::endl;

    TEST("line comment skipped", {
        auto t = tokenize("42 // comment\n");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "EOF_TOKEN ");
        return true;
    });
    TEST("line comment before token", {
        auto t = tokenize("// skip\n42");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "EOF_TOKEN ");
        return true;
    });
    TEST("line comment at EOF (no trailing newline)", {
        auto t = tokenize("42 // comment");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "EOF_TOKEN ");
        return true;
    });
    // C89 block comments
    TEST("block comment inline", {
        auto t = tokenize("42 /* comment */ 7");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "CONSTANT 7");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("block comment multiline", {
        auto t = tokenize("42 /* line1\nline2 */ 7");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "CONSTANT 7");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("block comment with stars", {
        auto t = tokenize("42 /*** comment ***/ 7");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "CONSTANT 7");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("unterminated block comment should error", {
        auto r = tokenize_with_errors("42 /* unterminated");
        EXPECT(r.errors.find("Unterminated") != std::string::npos);
        return true;
    });
}

// ============================================================
// 11. Whitespace handling
// ============================================================
void test_whitespace()
{
    std::cerr << "\n--- Whitespace ---" << std::endl;

    TEST("spaces between tokens", {
        auto t = tokenize("int x");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("tabs between tokens", {
        auto t = tokenize("int\tx");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("newlines between tokens", {
        auto t = tokenize("int\nx");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("carriage return ignored", {
        auto t = tokenize("int\r\nx");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("mixed whitespace", {
        auto t = tokenize("  int  \t\n  x  ");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "EOF_TOKEN ");
        return true;
    });
    TEST("empty input", {
        auto t = tokenize("");
        EXPECT_TOKEN(t, 0, "EOF_TOKEN ");
        return true;
    });
    TEST("only whitespace", {
        auto t = tokenize("   \t\n  ");
        EXPECT_TOKEN(t, 0, "EOF_TOKEN ");
        return true;
    });
}

// ============================================================
// 12. Multi-token sequences
// ============================================================
void test_sequences()
{
    std::cerr << "\n--- Multi-token Sequences ---" << std::endl;

    TEST("variable declaration", {
        auto t = tokenize("int x;");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER x");
        EXPECT_TOKEN(t, 2, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 3, "EOF_TOKEN ");
        return true;
    });
    TEST("assignment statement", {
        auto t = tokenize("x = 42;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "CONSTANT 42");
        EXPECT_TOKEN(t, 3, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 4, "EOF_TOKEN ");
        return true;
    });
    TEST("function call", {
        auto t = tokenize("foo(1, 2)");
        EXPECT_TOKEN(t, 0, "IDENTIFIER foo");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "CONSTANT 1");
        EXPECT_TOKEN(t, 3, "COMMA ,");
        EXPECT_TOKEN(t, 5, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 6, "EOF_TOKEN ");
        return true;
    });
    TEST("equality comparison", {
        auto t = tokenize("x == 0");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "EQ_OP ==");
        EXPECT_TOKEN(t, 2, "CONSTANT 0");
        EXPECT_TOKEN(t, 3, "EOF_TOKEN ");
        return true;
    });
    TEST("arithmetic expression", {
        auto t = tokenize("a + b * c");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "PLUS +");
        EXPECT_TOKEN(t, 2, "IDENTIFIER b");
        EXPECT_TOKEN(t, 3, "ASTERISK *");
        EXPECT_TOKEN(t, 4, "IDENTIFIER c");
        EXPECT_TOKEN(t, 5, "EOF_TOKEN ");
        return true;
    });
    TEST("negation", {
        auto t = tokenize("x = -1;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "MINUS -");
        EXPECT_TOKEN(t, 3, "CONSTANT 1");
        EXPECT_TOKEN(t, 4, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 5, "EOF_TOKEN ");
        return true;
    });
    TEST("consecutive operators no space", {
        auto t = tokenize("a+b");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "PLUS +");
        EXPECT_TOKEN(t, 2, "IDENTIFIER b");
        EXPECT_TOKEN(t, 3, "EOF_TOKEN ");
        return true;
    });
}

// ============================================================
// 13. C89 code snippets
// ============================================================
void test_c89_snippets()
{
    std::cerr << "\n--- C89 Snippets ---" << std::endl;

    TEST("minimal main", {
        auto t = tokenize("int main() { return 0; }");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER main");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 4, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 5, "RETURN return");
        EXPECT_TOKEN(t, 6, "CONSTANT 0");
        EXPECT_TOKEN(t, 7, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 8, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 9, "EOF_TOKEN ");
        return true;
    });

    TEST("if-else block", {
        auto t = tokenize("if (x != 0) { y = 1; } else { y = 0; }");
        EXPECT_TOKEN(t, 0, "IF if");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER x");
        EXPECT_TOKEN(t, 3, "NE_OP !=");
        EXPECT_TOKEN(t, 4, "CONSTANT 0");
        EXPECT_TOKEN(t, 5, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 6, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 7, "IDENTIFIER y");
        EXPECT_TOKEN(t, 8, "ASSIGN =");
        EXPECT_TOKEN(t, 9, "CONSTANT 1");
        EXPECT_TOKEN(t, 10, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 11, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 12, "ELSE else");
        EXPECT_TOKEN(t, 13, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 14, "IDENTIFIER y");
        EXPECT_TOKEN(t, 15, "ASSIGN =");
        EXPECT_TOKEN(t, 16, "CONSTANT 0");
        EXPECT_TOKEN(t, 17, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 18, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 19, "EOF_TOKEN ");
        return true;
    });

    TEST("while loop", {
        auto t = tokenize("while (i < 10) { i = i + 1; }");
        EXPECT_TOKEN(t, 0, "WHILE while");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER i");
        EXPECT_TOKEN(t, 3, "LESS_THAN <");
        EXPECT_TOKEN(t, 4, "CONSTANT 10");
        EXPECT_TOKEN(t, 5, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 6, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 7, "IDENTIFIER i");
        EXPECT_TOKEN(t, 8, "ASSIGN =");
        EXPECT_TOKEN(t, 9, "IDENTIFIER i");
        EXPECT_TOKEN(t, 10, "PLUS +");
        EXPECT_TOKEN(t, 11, "CONSTANT 1");
        EXPECT_TOKEN(t, 12, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 13, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 14, "EOF_TOKEN ");
        return true;
    });

    TEST("for loop (uses ; and comparison)", {
        auto t = tokenize("for (i = 0; i < 10; i = i + 1) {}");
        EXPECT_TOKEN(t, 0, "FOR for");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER i");
        EXPECT_TOKEN(t, 3, "ASSIGN =");
        EXPECT_TOKEN(t, 4, "CONSTANT 0");
        EXPECT_TOKEN(t, 5, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 6, "IDENTIFIER i");
        EXPECT_TOKEN(t, 7, "LESS_THAN <");
        EXPECT_TOKEN(t, 8, "CONSTANT 10");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 10, "IDENTIFIER i");
        EXPECT_TOKEN(t, 11, "ASSIGN =");
        EXPECT_TOKEN(t, 12, "IDENTIFIER i");
        EXPECT_TOKEN(t, 13, "PLUS +");
        EXPECT_TOKEN(t, 14, "CONSTANT 1");
        EXPECT_TOKEN(t, 15, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 16, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 17, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 18, "EOF_TOKEN ");
        return true;
    });

    TEST("string in function", {
        auto t = tokenize(R"(void f() { "hello"; })");
        EXPECT_TOKEN(t, 0, "VOID void");
        EXPECT_TOKEN(t, 1, "IDENTIFIER f");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 4, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 5, R"(STRING_LITERAL "hello")");
        EXPECT_TOKEN(t, 6, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 7, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 8, "EOF_TOKEN ");
        return true;
    });

    TEST("multiline function", {
        auto t = tokenize("int add(int a, int b) {\n"
                          "    return a + b;\n"
                          "}\n");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER add");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "INT int");
        EXPECT_TOKEN(t, 4, "IDENTIFIER a");
        EXPECT_TOKEN(t, 5, "COMMA ,");
        EXPECT_TOKEN(t, 6, "INT int");
        EXPECT_TOKEN(t, 7, "IDENTIFIER b");
        EXPECT_TOKEN(t, 8, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 9, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 10, "RETURN return");
        EXPECT_TOKEN(t, 11, "IDENTIFIER a");
        EXPECT_TOKEN(t, 12, "PLUS +");
        EXPECT_TOKEN(t, 13, "IDENTIFIER b");
        EXPECT_TOKEN(t, 14, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 15, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 16, "EOF_TOKEN ");
        return true;
    });
}

// ============================================================
// 14. Error cases
// ============================================================
void test_errors()
{
    std::cerr << "\n--- Error Cases ---" << std::endl;

    TEST("unterminated string", {
        auto r = tokenize_with_errors("\"hello");
        EXPECT(r.errors.find("Unterminated string") != std::string::npos);
        return true;
    });
    TEST("unexpected character '@'", {
        auto r = tokenize_with_errors("@");
        EXPECT(r.errors.find("Unexpected character") != std::string::npos);
        return true;
    });
    TEST("'#' directive line is skipped", {
        auto r = tokenize_with_errors("#define FOO 1");
        EXPECT(r.errors.empty());
        return true;
    });
    TEST("tokens still produced after error", {
        auto r = tokenize_with_errors("@ 42");
        bool hasConst = false;
        for (auto &tok : r.tokens)
            if (tok.type == CONSTANT && tok.lexeme == "42")
                hasConst = true;
        EXPECT(hasConst);
        return true;
    });
    TEST("unterminated char constant", {
        auto r = tokenize_with_errors("'a");
        EXPECT(r.errors.find("Unterminated") != std::string::npos);
        return true;
    });
    TEST("unterminated escape in char constant", {
        auto r = tokenize_with_errors("'\\n");
        EXPECT(r.errors.find("Unterminated") != std::string::npos);
        return true;
    });
    TEST("empty char constant '' should error", {
        auto r = tokenize_with_errors("''");
        EXPECT(r.errors.find("Empty") != std::string::npos);
        return true;
    });
    TEST("0x with no digits should error", {
        auto r = tokenize_with_errors("0x");
        EXPECT(r.errors.find("Hex constant requires at least one digit") != std::string::npos);
        return true;
    });
    TEST("1e with no exponent digits should error", {
        auto r = tokenize_with_errors("1e");
        EXPECT(r.errors.find("Exponent requires at least one digit") != std::string::npos);
        return true;
    });
    TEST("3.14e with no exponent digits should error", {
        auto r = tokenize_with_errors("3.14e");
        EXPECT(r.errors.find("Exponent requires at least one digit") != std::string::npos);
        return true;
    });
}

// ============================================================
// 15. Number parsing edge cases
// ============================================================
void test_number_edge_cases()
{
    std::cerr << "\n--- Number Edge Cases ---" << std::endl;

    TEST("hex mixed case", {
        auto t = tokenize("0xDeAdBeEf");
        EXPECT_TOKEN(t, 0, "CONSTANT 0xDeAdBeEf");
        return true;
    });
    TEST("octal with leading zeros", {
        auto t = tokenize("00");
        EXPECT_TOKEN(t, 0, "CONSTANT 00");
        return true;
    });
    TEST("float with positive exponent sign", {
        auto t = tokenize("1e+5");
        EXPECT_TOKEN(t, 0, "CONSTANT 1e+5");
        return true;
    });
    TEST("decimal float with exponent and sign", {
        auto t = tokenize("3.14e+2");
        EXPECT_TOKEN(t, 0, "CONSTANT 3.14e+2");
        return true;
    });
    TEST("two dots is two DOTs not ellipsis", {
        auto t = tokenize("..");
        EXPECT_TOKEN(t, 0, "DOT .");
        EXPECT_TOKEN(t, 1, "DOT .");
        return true;
    });
    TEST("1e2+3 should be three tokens", {
        auto t = tokenize("1e2+3");
        EXPECT_TOKEN(t, 0, "CONSTANT 1e2");
        EXPECT_TOKEN(t, 1, "PLUS +");
        EXPECT_TOKEN(t, 2, "CONSTANT 3");
        return true;
    });
    TEST("float suffix should be exactly one char", {
        auto t = tokenize("3.14ff");
        EXPECT_TOKEN(t, 0, "CONSTANT 3.14f");
        EXPECT_TOKEN(t, 1, "IDENTIFIER f");
        return true;
    });
}

// ============================================================
// 16. Operator disambiguation (maximal munch)
// ============================================================
void test_operator_disambiguation()
{
    std::cerr << "\n--- Operator Disambiguation ---" << std::endl;

    TEST("a+++b is a ++ + b", {
        auto t = tokenize("a+++b");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "INC_OP ++");
        EXPECT_TOKEN(t, 2, "PLUS +");
        EXPECT_TOKEN(t, 3, "IDENTIFIER b");
        return true;
    });
    TEST("a---b is a -- - b", {
        auto t = tokenize("a---b");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "DEC_OP --");
        EXPECT_TOKEN(t, 2, "MINUS -");
        EXPECT_TOKEN(t, 3, "IDENTIFIER b");
        return true;
    });
    TEST("a=--b is a = -- b", {
        auto t = tokenize("a=--b");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "DEC_OP --");
        EXPECT_TOKEN(t, 3, "IDENTIFIER b");
        return true;
    });
    TEST("*/ outside comment is ASTERISK SLASH", {
        auto t = tokenize("*/");
        EXPECT_TOKEN(t, 0, "ASTERISK *");
        EXPECT_TOKEN(t, 1, "SLASH /");
        return true;
    });
    TEST("<<= is LEFT_ASSIGN not LEFT_OP ASSIGN", {
        auto t = tokenize("x<<=1");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "LEFT_ASSIGN <<=");
        EXPECT_TOKEN(t, 2, "CONSTANT 1");
        return true;
    });
    TEST(">>= is RIGHT_ASSIGN not RIGHT_OP ASSIGN", {
        auto t = tokenize("x>>=1");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "RIGHT_ASSIGN >>=");
        EXPECT_TOKEN(t, 2, "CONSTANT 1");
        return true;
    });
}

// ============================================================
// 17. Parser-readiness: realistic C89 programs
// ============================================================
void test_parser_readiness()
{
    std::cerr << "\n--- Parser-Readiness Snippets ---" << std::endl;

    TEST("pointer declaration and dereference", {
        auto t = tokenize("int *p; *p = 42;");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "ASTERISK *");
        EXPECT_TOKEN(t, 2, "IDENTIFIER p");
        EXPECT_TOKEN(t, 3, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 4, "ASTERISK *");
        EXPECT_TOKEN(t, 5, "IDENTIFIER p");
        EXPECT_TOKEN(t, 6, "ASSIGN =");
        EXPECT_TOKEN(t, 7, "CONSTANT 42");
        EXPECT_TOKEN(t, 8, "SEMI_COLON ;");
        return true;
    });
    TEST("struct member access", {
        auto t = tokenize("s.x = p->y;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER s");
        EXPECT_TOKEN(t, 1, "DOT .");
        EXPECT_TOKEN(t, 2, "IDENTIFIER x");
        EXPECT_TOKEN(t, 3, "ASSIGN =");
        EXPECT_TOKEN(t, 4, "IDENTIFIER p");
        EXPECT_TOKEN(t, 5, "PTR_OP ->");
        EXPECT_TOKEN(t, 6, "IDENTIFIER y");
        EXPECT_TOKEN(t, 7, "SEMI_COLON ;");
        return true;
    });
    TEST("array subscript", {
        auto t = tokenize("a[i] = b[0];");
        EXPECT_TOKEN(t, 0, "IDENTIFIER a");
        EXPECT_TOKEN(t, 1, "LEFT_BRACKET [");
        EXPECT_TOKEN(t, 2, "IDENTIFIER i");
        EXPECT_TOKEN(t, 3, "RIGHT_BRACKET ]");
        EXPECT_TOKEN(t, 4, "ASSIGN =");
        EXPECT_TOKEN(t, 5, "IDENTIFIER b");
        EXPECT_TOKEN(t, 6, "LEFT_BRACKET [");
        EXPECT_TOKEN(t, 7, "CONSTANT 0");
        EXPECT_TOKEN(t, 8, "RIGHT_BRACKET ]");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        return true;
    });
    TEST("ternary expression", {
        auto t = tokenize("x = a > b ? a : b;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "IDENTIFIER a");
        EXPECT_TOKEN(t, 3, "GREATER_THAN >");
        EXPECT_TOKEN(t, 4, "IDENTIFIER b");
        EXPECT_TOKEN(t, 5, "QUESTION_MARK ?");
        EXPECT_TOKEN(t, 6, "IDENTIFIER a");
        EXPECT_TOKEN(t, 7, "COLON :");
        EXPECT_TOKEN(t, 8, "IDENTIFIER b");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        return true;
    });
    TEST("cast expression", {
        auto t = tokenize("x = (int)y;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "INT int");
        EXPECT_TOKEN(t, 4, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 5, "IDENTIFIER y");
        EXPECT_TOKEN(t, 6, "SEMI_COLON ;");
        return true;
    });
    TEST("sizeof expression", {
        auto t = tokenize("n = sizeof(int);");
        EXPECT_TOKEN(t, 0, "IDENTIFIER n");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "SIZEOF sizeof");
        EXPECT_TOKEN(t, 3, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 4, "INT int");
        EXPECT_TOKEN(t, 5, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 6, "SEMI_COLON ;");
        return true;
    });
    TEST("address-of and bitwise AND", {
        auto t = tokenize("p = &x; y = a & b;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER p");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "AMPERSAND &");
        EXPECT_TOKEN(t, 3, "IDENTIFIER x");
        EXPECT_TOKEN(t, 4, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 5, "IDENTIFIER y");
        EXPECT_TOKEN(t, 6, "ASSIGN =");
        EXPECT_TOKEN(t, 7, "IDENTIFIER a");
        EXPECT_TOKEN(t, 8, "AMPERSAND &");
        EXPECT_TOKEN(t, 9, "IDENTIFIER b");
        EXPECT_TOKEN(t, 10, "SEMI_COLON ;");
        return true;
    });
    TEST("compound assignment operators", {
        auto t = tokenize("x += 1; y -= 2; z *= 3; w /= 4; v %= 5;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ADD_ASSIGN +=");
        EXPECT_TOKEN(t, 2, "CONSTANT 1");
        EXPECT_TOKEN(t, 3, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 4, "IDENTIFIER y");
        EXPECT_TOKEN(t, 5, "SUB_ASSIGN -=");
        EXPECT_TOKEN(t, 6, "CONSTANT 2");
        EXPECT_TOKEN(t, 7, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 8, "IDENTIFIER z");
        EXPECT_TOKEN(t, 9, "MUL_ASSIGN *=");
        EXPECT_TOKEN(t, 10, "CONSTANT 3");
        EXPECT_TOKEN(t, 11, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 12, "IDENTIFIER w");
        EXPECT_TOKEN(t, 13, "DIV_ASSIGN /=");
        EXPECT_TOKEN(t, 14, "CONSTANT 4");
        EXPECT_TOKEN(t, 15, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 16, "IDENTIFIER v");
        EXPECT_TOKEN(t, 17, "MOD_ASSIGN %=");
        EXPECT_TOKEN(t, 18, "CONSTANT 5");
        EXPECT_TOKEN(t, 19, "SEMI_COLON ;");
        return true;
    });
    TEST("bitwise operators in expression", {
        auto t = tokenize("x = a | b & c ^ ~d;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "IDENTIFIER a");
        EXPECT_TOKEN(t, 3, "PIPE |");
        EXPECT_TOKEN(t, 4, "IDENTIFIER b");
        EXPECT_TOKEN(t, 5, "AMPERSAND &");
        EXPECT_TOKEN(t, 6, "IDENTIFIER c");
        EXPECT_TOKEN(t, 7, "CARET ^");
        EXPECT_TOKEN(t, 8, "TILDE ~");
        EXPECT_TOKEN(t, 9, "IDENTIFIER d");
        EXPECT_TOKEN(t, 10, "SEMI_COLON ;");
        return true;
    });
    TEST("shift operators", {
        auto t = tokenize("x = a << 2; y = b >> 3;");
        EXPECT_TOKEN(t, 0, "IDENTIFIER x");
        EXPECT_TOKEN(t, 1, "ASSIGN =");
        EXPECT_TOKEN(t, 2, "IDENTIFIER a");
        EXPECT_TOKEN(t, 3, "LEFT_OP <<");
        EXPECT_TOKEN(t, 4, "CONSTANT 2");
        EXPECT_TOKEN(t, 5, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 6, "IDENTIFIER y");
        EXPECT_TOKEN(t, 7, "ASSIGN =");
        EXPECT_TOKEN(t, 8, "IDENTIFIER b");
        EXPECT_TOKEN(t, 9, "RIGHT_OP >>");
        EXPECT_TOKEN(t, 10, "CONSTANT 3");
        EXPECT_TOKEN(t, 11, "SEMI_COLON ;");
        return true;
    });
    TEST("struct definition", {
        auto t = tokenize("struct point { int x; int y; };");
        EXPECT_TOKEN(t, 0, "STRUCT struct");
        EXPECT_TOKEN(t, 1, "IDENTIFIER point");
        EXPECT_TOKEN(t, 2, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 3, "INT int");
        EXPECT_TOKEN(t, 4, "IDENTIFIER x");
        EXPECT_TOKEN(t, 5, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 6, "INT int");
        EXPECT_TOKEN(t, 7, "IDENTIFIER y");
        EXPECT_TOKEN(t, 8, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 9, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 10, "SEMI_COLON ;");
        return true;
    });
    TEST("for loop with increment operator", {
        auto t = tokenize("for (i = 0; i < n; i++) {}");
        EXPECT_TOKEN(t, 0, "FOR for");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER i");
        EXPECT_TOKEN(t, 3, "ASSIGN =");
        EXPECT_TOKEN(t, 4, "CONSTANT 0");
        EXPECT_TOKEN(t, 5, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 6, "IDENTIFIER i");
        EXPECT_TOKEN(t, 7, "LESS_THAN <");
        EXPECT_TOKEN(t, 8, "IDENTIFIER n");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 10, "IDENTIFIER i");
        EXPECT_TOKEN(t, 11, "INC_OP ++");
        EXPECT_TOKEN(t, 12, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 13, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 14, "RIGHT_BRACE }");
        return true;
    });
    TEST("logical operators", {
        auto t = tokenize("if (a && b || !c) {}");
        EXPECT_TOKEN(t, 0, "IF if");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER a");
        EXPECT_TOKEN(t, 3, "AND_OP &&");
        EXPECT_TOKEN(t, 4, "IDENTIFIER b");
        EXPECT_TOKEN(t, 5, "OR_OP ||");
        EXPECT_TOKEN(t, 6, "EXCLAMATION !");
        EXPECT_TOKEN(t, 7, "IDENTIFIER c");
        EXPECT_TOKEN(t, 8, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 9, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 10, "RIGHT_BRACE }");
        return true;
    });
    TEST("function with multiple params and body", {
        auto t = tokenize("int max(int a, int b) {\n"
                          "    if (a > b)\n"
                          "        return a;\n"
                          "    return b;\n"
                          "}\n");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER max");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "INT int");
        EXPECT_TOKEN(t, 4, "IDENTIFIER a");
        EXPECT_TOKEN(t, 5, "COMMA ,");
        EXPECT_TOKEN(t, 6, "INT int");
        EXPECT_TOKEN(t, 7, "IDENTIFIER b");
        EXPECT_TOKEN(t, 8, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 9, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 10, "IF if");
        EXPECT_TOKEN(t, 11, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 12, "IDENTIFIER a");
        EXPECT_TOKEN(t, 13, "GREATER_THAN >");
        EXPECT_TOKEN(t, 14, "IDENTIFIER b");
        EXPECT_TOKEN(t, 15, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 16, "RETURN return");
        EXPECT_TOKEN(t, 17, "IDENTIFIER a");
        EXPECT_TOKEN(t, 18, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 19, "RETURN return");
        EXPECT_TOKEN(t, 20, "IDENTIFIER b");
        EXPECT_TOKEN(t, 21, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 22, "RIGHT_BRACE }");
        return true;
    });
    TEST("comment-like content inside string literal", {
        auto t = tokenize("\"hello /* not a comment */ world\"");
        EXPECT_TOKEN(t, 0, R"(STRING_LITERAL "hello /* not a comment */ world")");
        return true;
    });
    TEST("string-like content inside comment", {
        auto t = tokenize("/* \"not a string\" */ 42");
        EXPECT_TOKEN(t, 0, "CONSTANT 42");
        EXPECT_TOKEN(t, 1, "EOF_TOKEN ");
        return true;
    });
    TEST("do-while loop", {
        auto t = tokenize("do { x++; } while (x < 10);");
        EXPECT_TOKEN(t, 0, "DO do");
        EXPECT_TOKEN(t, 1, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 2, "IDENTIFIER x");
        EXPECT_TOKEN(t, 3, "INC_OP ++");
        EXPECT_TOKEN(t, 4, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 5, "RIGHT_BRACE }");
        EXPECT_TOKEN(t, 6, "WHILE while");
        EXPECT_TOKEN(t, 7, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 8, "IDENTIFIER x");
        EXPECT_TOKEN(t, 9, "LESS_THAN <");
        EXPECT_TOKEN(t, 10, "CONSTANT 10");
        EXPECT_TOKEN(t, 11, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 12, "SEMI_COLON ;");
        return true;
    });
    TEST("switch-case", {
        auto t = tokenize("switch (x) {\n"
                          "    case 1: break;\n"
                          "    default: break;\n"
                          "}\n");
        EXPECT_TOKEN(t, 0, "SWITCH switch");
        EXPECT_TOKEN(t, 1, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 2, "IDENTIFIER x");
        EXPECT_TOKEN(t, 3, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 4, "LEFT_BRACE {");
        EXPECT_TOKEN(t, 5, "CASE case");
        EXPECT_TOKEN(t, 6, "CONSTANT 1");
        EXPECT_TOKEN(t, 7, "COLON :");
        EXPECT_TOKEN(t, 8, "BREAK break");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 10, "DEFAULT default");
        EXPECT_TOKEN(t, 11, "COLON :");
        EXPECT_TOKEN(t, 12, "BREAK break");
        EXPECT_TOKEN(t, 13, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 14, "RIGHT_BRACE }");
        return true;
    });
    TEST("goto statement", {
        auto t = tokenize("goto done; done: return 0;");
        EXPECT_TOKEN(t, 0, "GOTO goto");
        EXPECT_TOKEN(t, 1, "IDENTIFIER done");
        EXPECT_TOKEN(t, 2, "SEMI_COLON ;");
        EXPECT_TOKEN(t, 3, "IDENTIFIER done");
        EXPECT_TOKEN(t, 4, "COLON :");
        EXPECT_TOKEN(t, 5, "RETURN return");
        EXPECT_TOKEN(t, 6, "CONSTANT 0");
        EXPECT_TOKEN(t, 7, "SEMI_COLON ;");
        return true;
    });
    TEST("variadic function declaration", {
        auto t = tokenize("int printf(char *fmt, ...);");
        EXPECT_TOKEN(t, 0, "INT int");
        EXPECT_TOKEN(t, 1, "IDENTIFIER printf");
        EXPECT_TOKEN(t, 2, "LEFT_PAREN (");
        EXPECT_TOKEN(t, 3, "CHAR char");
        EXPECT_TOKEN(t, 4, "ASTERISK *");
        EXPECT_TOKEN(t, 5, "IDENTIFIER fmt");
        EXPECT_TOKEN(t, 6, "COMMA ,");
        EXPECT_TOKEN(t, 7, "ELLIPSIS ...");
        EXPECT_TOKEN(t, 8, "RIGHT_PAREN )");
        EXPECT_TOKEN(t, 9, "SEMI_COLON ;");
        return true;
    });
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cerr << "C89 Lexer Test Suite" << std::endl;
    std::cerr << "====================" << std::endl;

    test_keywords();
    test_identifiers();
    test_integer_constants();
    test_float_constants();
    test_char_constants();
    test_string_literals();
    test_single_char_tokens();
    test_two_char_operators();
    test_three_char_tokens();
    test_comments();
    test_whitespace();
    test_sequences();
    test_c89_snippets();
    test_errors();
    test_number_edge_cases();
    test_operator_disambiguation();
    test_parser_readiness();

    std::cerr << "\n====================" << std::endl;
    std::cerr << "Passed: " << g_passed << "/" << g_total << std::endl;
    std::cerr << "Failed: " << g_failed << "/" << g_total << std::endl;

    return g_failed > 0 ? 1 : 0;
}
