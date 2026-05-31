#pragma once
#include "TackyTopLevelNode.h"
#include <string>

class TackyStaticVariable : public TackyTopLevelNode
{
  public:
    std::string identifier;
    bool global;
    int init;
    TackyStaticVariable(int line_, int col_, std::string identifier_, bool global_, int init_)
        : TackyTopLevelNode(line_, col_), identifier(std::move(identifier_)), global(global_),
          init(init_)
    {
    }
};