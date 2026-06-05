#include "ast/ASTNodes/Program.h"
#include "ast/visitors/ASTDebugPrinter.h"
#include "codegen/ast/visitors/codegenASTPrinter.h"
#include "codegen/codegen.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semanticanalyzer/SemanticAnalyzer.h"
#include "tacky/ast/visitors/TackyDebugPrinter.h"
#include "tacky/tacky.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

static const std::string PRELUDE_SOURCE = "void *malloc(int size);\n"
                                          "void free(void *p);\n"
                                          "int printf(char *fmt, ...);\n"
                                          "int scanf(char *fmt, ...);\n";

// Stages, in pipeline order. Default is the last one wired up.
//   --lex       lex only; print tokens
//   --parse     lex + parse
//   --validate  lex + parse + SA
//   --tacky     lex + parse + SA + TACKY IR
//   --codegen   lex + parse + SA + TACKY + codegen (prints asm to stdout)
//   --compile   full pipeline, then assemble + link with gcc into an executable   (default)
//
// Flags:
//   --debugAST    print AST after parse / validate (off by default)
//   --debugTacky  print TACKY IR after the tacky pass (off by default)
//   -l<lib>       link against <lib> (passed through to gcc); repeatable, e.g. -lm
static int run(const std::string &inputSourcePath, const std::string &stage, bool debugAST,
               bool debugTacky, const std::vector<std::string> &libs)
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
        for (const auto &token : tokens)
            std::cout << token.toString() << "\n";
        return 0;
    }

    Parser parser{tokens};
    std::shared_ptr<Program> p = parser.ParseProgram();
    ASTDebugPrinter dbg(std::cout);

    if (stage == "parse")
    {
        if (debugAST)
            dbg.print(p);
        return 0;
    }

    if (stage == "validate" || stage == "tacky" || stage == "codegen" || stage == "compile")
    {
        Diagnostic::DiagnosticEngine preludeDiag{"<prelude>"};
        Lexer preludeLexer{PRELUDE_SOURCE, preludeDiag};
        Parser preludeParser{preludeLexer.generateTokens()};
        std::shared_ptr<Program> preludeProgram = preludeParser.ParseProgram();
        p->nodes.insert(p->nodes.begin(), preludeProgram->nodes.begin(),
                        preludeProgram->nodes.end());

        SemanticAnalyzer semanticAnalyzer{diagnosticEngine};
        semanticAnalyzer.validate(p);
        if (diagnosticEngine.hasErrors())
        {
            diagnosticEngine.print();
            return 1;
        }
        if (debugAST)
            dbg.print(p);

        if (stage == "validate")
            return 0;

        TackyDriver tackyDriver;
        auto tackyProg = tackyDriver.tacky(p);
        if (debugTacky)
            TackyDebugPrinter(std::cout).print(*tackyProg);

        if (stage == "tacky")
            return 0;

        codegenDriver driver;
        auto cgProgram = driver.codegen(*tackyProg);

        // --codegen: emit assembly to stdout (the test harness captures this).
        if (stage == "codegen")
        {
            codegenASTPrinter(std::cout).print(*cgProgram);
            return 0;
        }

        // --compile (default): emit the assembly to a sibling .s, then hand it to
        // gcc to assemble + link into an executable named after the source.
        std::ostringstream asmBuffer;
        codegenASTPrinter(asmBuffer).print(*cgProgram);

        std::string base = inputSourcePath;
        if (base.size() > 2 && base.compare(base.size() - 2, 2, ".c") == 0)
            base.erase(base.size() - 2);
        const std::string asmPath = base + ".s";
        const std::string exePath = base;

        std::ofstream asmOut(asmPath);
        if (!asmOut.is_open())
            throw std::runtime_error("failed to write assembly: " + asmPath);
        asmOut << asmBuffer.str();
        asmOut.close();

        std::string command = "gcc " + asmPath + " -o " + exePath;
        for (const std::string &lib : libs)
            command += " " + lib;

        int gccStatus = std::system(command.c_str());
        std::remove(asmPath.c_str());
        if (gccStatus != 0)
        {
            std::cerr << "assemble/link step failed: " << command << "\n";
            return 1;
        }
        return 0;
    }

    std::cerr << "unknown stage: --" << stage << "\n";
    return 2;
}

int main(int argc, char **argv)
{
    std::string stage = "compile";
    std::string path;
    std::vector<std::string> libs;
    bool debugAST = false;
    bool debugTacky = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--debugAST")
            debugAST = true;
        else if (a == "--debugTacky")
            debugTacky = true;
        else if (a.rfind("--", 0) == 0)
            stage = a.substr(2);
        else if (a.rfind("-l", 0) == 0)
            libs.push_back(a);
        else
            path = a;
    }
    if (path.empty())
    {
        std::cerr << "Usage: cc89 [--lex|--parse|--validate|--tacky|--codegen|--compile] "
                     "[--debugAST] [--debugTacky] [-l<lib>...] <source.c>\n";
        return 1;
    }

    try
    {
        return run(path, stage, debugAST, debugTacky, libs);
    }
    catch (const std::exception &e)
    {
        std::cerr << path << ": error: " << e.what() << "\n";
        return 1;
    }
}
