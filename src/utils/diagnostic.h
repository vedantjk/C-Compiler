#pragma once
#include <iostream>
#include <string>
#include <vector>

namespace Diagnostic
{

enum class DiagLevel
{
    LEXER,
    PARSER,
    SEMANTIC
};

inline const char *to_string(DiagLevel level)
{
    switch (level)
    {
    case DiagLevel::LEXER:
        return "Lexer";
    case DiagLevel::PARSER:
        return "Parser";
    case DiagLevel::SEMANTIC:
        return "Semantic";
    }
    return "Unknown";
}

struct Location
{
    int line;
    int column;
};

struct Diagnostic
{
    DiagLevel level;
    Location location;
    std::string message;
};

class DiagnosticEngine
{
    std::vector<Diagnostic> errors;
    std::string filename;

  public:
    DiagnosticEngine() = default;
    explicit DiagnosticEngine(std::string filename) : filename(std::move(filename)) {}
    DiagnosticEngine(const DiagnosticEngine &) = delete;
    DiagnosticEngine &operator=(const DiagnosticEngine &) = delete;
    void report(DiagLevel level, Location loc, std::string msg);
    bool hasErrors() const;
    void print();
};

} // namespace Diagnostic