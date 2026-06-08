#pragma once

#include "../../../types/types.h"
#include "codegenTopLevelNode.h"
#include <string>
#include <vector>

class codegenStaticVariable : public codegenTopLevelNode
{
  public:
    std::string name;
    bool global;
    // Flat data image (scalars: one entry; arrays: a sequence + trailing zero).
    std::vector<StaticInit> inits;
    int align;

    codegenStaticVariable(const int line_, const int col_, std::string name_, bool global_,
                          std::vector<StaticInit> inits_, int align_)
        : codegenTopLevelNode(line_, col_), name(std::move(name_)), global(global_),
          inits(std::move(inits_)), align(align_)
    {
    }
};
