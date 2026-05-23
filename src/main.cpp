#include "ast/ASTNodes/Program.h"
#include "ast/visitors/ASTDebugPrinter.h"
#include "codegen/ast/visitors/codegenASTPrinter.h"
#include "codegen/codegen.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semanticanalyzer/SemanticAnalyzer.h"
#include "tacky/tacky.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

static const std::string PRELUDE_SOURCE =
    "void *malloc(int size);\n"
    "void free(void *p);\n"
    "int printf(char *fmt, ...);\n"
    "int scanf(char *fmt, ...);\n";

// Stages, in pipeline order. Default is the last one wired up.
//   --lex       lex only; print tokens
//   --parse     lex + parse
//   --validate  lex + parse + SA
//   --tacky     lex + parse + SA + TACKY IR
//   --codegen   lex + parse + SA + TACKY + codegen (prints asm to stdout)   (default)
//   --compile   alias for the latest stage (currently --codegen)
//
// Flags:
//   --debugAST  print AST after parse / validate (off by default)
static int run(const std::string &inputSourcePath, const std::string &stage, bool debugAST)
{
    std::ifstream inputFile(inputSourcePath);
    if (!inputFile.is_open())
    {
        throw std::runtime_error("Failed to open file: " + inputSourcePath);
    }

    std::stringstream buffer;
    buffer << inputFile.rdbuf();
    inputFile.close();

    Diagnostic::DiagnosticEngine diagnosticEngine{inputSourcePath};
    Lexer lexer{buffer.str(), diagnosticEngine};
    auto tokens = lexer.generateTokens();

    if (diagnosticEngine.hasErrors())
    {
        diagnosticEngine.print();
        return 1;
    }

    if (stage == "lex")
    {
        for (const auto &token : tokens) std::cout << token.toString() << "\n";
        return 0;
    }

    Parser parser{tokens};
    std::shared_ptr<Program> p = parser.ParseProgram();
    ASTDebugPrinter dbg(std::cout);

    if (stage == "parse")
    {
        if (debugAST) dbg.print(p);
        return 0;
    }

    if (stage == "validate" || stage == "tacky" || stage == "codegen" || stage == "compile")
    {
        Diagnostic::DiagnosticEngine preludeDiag{"<prelude>"};
        Lexer preludeLexer{PRELUDE_SOURCE, preludeDiag};
        Parser preludeParser{preludeLexer.generateTokens()};
        std::shared_ptr<Program> preludeProgram = preludeParser.ParseProgram();
        p->nodes.insert(p->nodes.begin(),
                        preludeProgram->nodes.begin(),
                        preludeProgram->nodes.end());

        SemanticAnalyzer semanticAnalyzer{diagnosticEngine};
        semanticAnalyzer.validate(p);
        if (diagnosticEngine.hasErrors())
        {
            diagnosticEngine.print();
            return 1;
        }
        if (debugAST) dbg.print(p);

        if (stage == "validate") return 0;

        TackyDriver tackyDriver;
        auto tackyProg = tackyDriver.tacky(p);

        if (stage == "tacky") return 0;

        codegenDriver driver;
        auto cgProgram = driver.codegen(*tackyProg);
        codegenASTPrinter printer(std::cout);
        printer.print(*cgProgram);
        return 0;
    }

    std::cerr << "unknown stage: --" << stage << "\n";
    return 2;
}

int main(int argc, char **argv)
{
    std::string stage = "codegen";
    std::string path;
    bool debugAST = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--debugAST") debugAST = true;
        else if (a.rfind("--", 0) == 0) stage = a.substr(2);
        else path = a;
    }
    if (path.empty())
    {
        std::cerr << "Usage: cc89 [--lex|--parse|--validate|--tacky|--codegen|--compile] [--debugAST] <source.c>\n";
        return 1;
    }

    try
    {
        return run(path, stage, debugAST);
    }
    catch (const std::exception &e)
    {
        std::cerr << path << ": error: " << e.what() << "\n";
        return 1;
    }
}
