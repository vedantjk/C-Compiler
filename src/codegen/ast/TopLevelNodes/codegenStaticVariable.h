#pragma once

#include "../../../tacky/instructions/val.h"
#include "codegenTopLevelNode.h"
#include <string>

class codegenStaticVariable : public codegenTopLevelNode
{
  public:
    std::string name;
    bool global;
    long long init;
    ConstantType type; // INT -> .long, LONG -> .quad

    codegenStaticVariable(const int line_, const int col_, std::string name_, bool global_,
                          long long init_, ConstantType type_)
        : codegenTopLevelNode(line_, col_), name(std::move(name_)), global(global_), init(init_),
          type(type_)
    {
    }
};