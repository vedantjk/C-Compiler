#include "ast/ASTNodes/Program.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "semanticanalyzer/SemanticAnalyzer.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

void test(const std::string &inputSourcePath)
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

    if (diagnosticEngine.hasErrors())
    {
        diagnosticEngine.print();
        return;
    }

    auto tokens = lexer.generateTokens();
    for (const auto &token : tokens)
    {
        std::cout << token.toString() << std::endl;
    }
    Parser parser{tokens};
    std::shared_ptr<Program> p = parser.ParseProgram();
    p->print(std::cout, 0);
    SemanticAnalyzer semanticAnalyzer;
    semanticAnalyzer.validate(p);
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: cc89 <source.c>\n";
        return 1;
    }

    std::cout << "cc89: " << argv[1] << "\n";
    test(argv[1]);
}
