#include "ast/ASTNodes/Program.h"
#include "ast/visitors/ASTDebugPrinter.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semanticanalyzer/SemanticAnalyzer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// Stages, in pipeline order. Default is the last one wired up.
//   --lex       lex only; print tokens
//   --parse     lex + parse
//   --validate  lex + parse + SA   (default)
//   --compile   alias for the latest stage (currently --validate)
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

    Diagnostic::DiagnosticEngine diagnosticEngine;
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

    if (stage == "validate" || stage == "compile")
    {
        SemanticAnalyzer semanticAnalyzer;
        semanticAnalyzer.validate(p);
        if (debugAST) dbg.print(p);
        return 0;
    }

    std::cerr << "unknown stage: --" << stage << "\n";
    return 2;
}

int main(int argc, char **argv)
{
    std::string stage = "validate";
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
        std::cerr << "Usage: cc89 [--lex|--parse|--validate|--compile] [--debugAST] <source.c>\n";
        return 1;
    }

    return run(path, stage, debugAST);
}
