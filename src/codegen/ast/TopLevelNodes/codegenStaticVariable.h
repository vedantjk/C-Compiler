#pragma once

#include "../../../tacky/instructions/val.h"
#include "codegenTopLevelNode.h"
#include <string>
#include <variant>

class codegenStaticVariable : public codegenTopLevelNode
{
  public:
    std::string name;
    bool global;
    // Integer payload (held as long long) or a double; `type` says which is live.
    std::variant<long long, double> init;
    ConstantType type; // INT -> .long, LONG -> .quad, DOUBLE -> .double

    codegenStaticVariable(const int line_, const int col_, std::string name_, bool global_,
                          std::variant<long long, double> init_, ConstantType type_)
        : codegenTopLevelNode(line_, col_), name(std::move(name_)), global(global_),
          init(std::move(init_)), type(type_)
    {
    }
};