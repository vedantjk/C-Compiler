#include "lexer/lexer.h"

#include <cstdlib>
#include <stdexcept>
#include <fstream>
#include <sstream>
#include <iostream>

void test(const std::string& inputSourcePath){
    std::ifstream inputFile(inputSourcePath);
    if(!inputFile.is_open()){
        throw std::runtime_error("Failed to open file: " + inputSourcePath);
    }

    std::stringstream buffer;
    buffer << inputFile.rdbuf();
    inputFile.close();

    Lexer lexer{buffer.str()};
    auto tokens = lexer.generateTokens();
    for(const auto& token : tokens){
        std::cout << token.toString() << std::endl;
    }
}

int main(int argc, char** argv){
    if (argc < 2) {
        std::cerr << "Usage: cc89 <source.c>\n";
        return 1;
    }

    std::cout << "cc89: " << argv[1] << "\n";
    test(argv[1]);
}
