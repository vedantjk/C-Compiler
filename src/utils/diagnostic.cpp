#include "diagnostic.h"

namespace Diagnostic
{
void DiagnosticEngine::report(DiagLevel level, Location loc, std::string msg)
{
    this->errors.push_back(Diagnostic{level, loc, std::move(msg)});
}

bool DiagnosticEngine::hasErrors() const { return !this->errors.empty(); }

void DiagnosticEngine::print()
{
    const std::string src = filename.empty() ? std::string("<input>") : filename;
    for (const auto &e : this->errors)
    {
        std::cerr << src << ':' << e.location.line << ':' << e.location.column
                  << ": error: " << e.message << '\n';
    }
}
} // namespace Diagnostic