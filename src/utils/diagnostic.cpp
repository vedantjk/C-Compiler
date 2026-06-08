#include "diagnostic.h"

namespace Diagnostic
{

// Returns the 1-based Nth line of `source` (splits on '\n').
// Returns an empty string if n is out of range.
std::string DiagnosticEngine::getLine(int n) const
{
    if (n < 1)
        return {};
    int current = 1;
    std::size_t start = 0;
    while (start <= source.size())
    {
        std::size_t end = source.find('\n', start);
        if (end == std::string::npos)
            end = source.size();
        if (current == n)
            return source.substr(start, end - start);
        ++current;
        start = end + 1;
    }
    return {};
}

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

        if (source.empty())
            continue;

        // Count total lines to detect out-of-range line numbers.
        // A source ending without a trailing newline still has at least one line.
        int totalLines = 1;
        for (char c : source)
            if (c == '\n')
                ++totalLines;
        // Subtract one if source ends with '\n' (that trailing newline adds a phantom line).
        if (!source.empty() && source.back() == '\n')
            --totalLines;

        if (e.location.line < 1 || e.location.line > totalLines)
            continue;

        std::string raw = getLine(e.location.line);

        // Expand tabs to TAB_WIDTH spaces for display.
        std::string expanded;
        expanded.reserve(raw.size());
        for (char c : raw)
        {
            if (c == '\t')
                expanded.append(TAB_WIDTH, ' ');
            else
                expanded += c;
        }

        // Clamp column: col < 1 → 1; col-1 > expanded length → past end.
        int col = e.location.column;
        if (col < 1)
            col = 1;
        int caretPos = col - 1;
        if (caretPos > static_cast<int>(expanded.size()))
            caretPos = static_cast<int>(expanded.size());

        const std::string gutter(4, ' ');
        std::cerr << gutter << expanded << '\n';
        std::cerr << gutter << std::string(caretPos, ' ') << '^' << '\n';
    }
}

} // namespace Diagnostic
