#pragma once

#include "codegenTopLevelNode.h"
#include <string>

class codegenStaticVariable : public codegenTopLevelNode
{
  public:
    std::string name;
    bool global;
    int init;

    codegenStaticVariable(const int line_, const int col_, std::string name_, bool global_,
                          int init_)
        : codegenTopLevelNode(line_, col_), name(std::move(name_)), global(global_), init(init_)
    {
    }
};