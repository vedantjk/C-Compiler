#pragma once
#include "../../../types/types.h"
#include "TackyTopLevelNode.h"
#include <string>
#include <vector>

class TackyStaticVariable : public TackyTopLevelNode
{
  public:
    std::string identifier;
    bool global;
    // Flat data image (scalars: one entry; arrays: a sequence + trailing zero).
    std::vector<StaticInit> inits;
    int align;
    TackyStaticVariable(int line_, int col_, std::string identifier_, bool global_,
                        std::vector<StaticInit> inits_, int align_)
        : TackyTopLevelNode(line_, col_), identifier(std::move(identifier_)), global(global_),
          inits(std::move(inits_)), align(align_)
    {
    }
};
