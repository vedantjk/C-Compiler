#include "diagnostic.h"

namespace Diagnostic
{
void DiagnosticEngine::report(DiagLevel level, Location loc, std::string msg)
{
    this->errors.push_back(Diagnostic{level, loc, msg});
}

bool DiagnosticEngine::hasErrors() const { return this->errors.size() > 0; }

void DiagnosticEngine::print()
{
    for (const auto &error : this->errors)
    {
        error.print();
    }
}
} // namespace Diagnostic