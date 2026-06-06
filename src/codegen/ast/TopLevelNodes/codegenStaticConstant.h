#pragma once

#include "codegenTopLevelNode.h"
#include <string>

// A read-only constant emitted into .rodata. Doubles can't be SSE immediates, so
// every double literal used in code becomes a labelled constant loaded via
// `movsd label(%rip), %xmm`; the sign-bit mask used for floating-point negation
// (`xorpd`) is the same kind of node, just 16-byte aligned.
class codegenStaticConstant : public codegenTopLevelNode
{
  public:
    std::string name;
    int alignment; // 8 for a double literal, 16 for the negation mask
    double value;

    codegenStaticConstant(const int line_, const int col_, std::string name_, const int alignment_,
                          const double value_)
        : codegenTopLevelNode(line_, col_), name(std::move(name_)), alignment(alignment_),
          value(value_)
    {
    }
};
