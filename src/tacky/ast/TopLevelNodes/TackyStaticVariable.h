#pragma once
#include "../../instructions/val.h"
#include "TackyTopLevelNode.h"
#include <string>
#include <variant>

class TackyStaticVariable : public TackyTopLevelNode
{
  public:
    std::string identifier;
    bool global;
    // Integer payload (held as long long) or a double; `type` says which is live.
    std::variant<long long, double> init;
    ConstantType type; // declared type: emit .long (INT), .quad (LONG), or .double (DOUBLE)
    TackyStaticVariable(int line_, int col_, std::string identifier_, bool global_,
                        std::variant<long long, double> init_, ConstantType type_)
        : TackyTopLevelNode(line_, col_), identifier(std::move(identifier_)), global(global_),
          init(std::move(init_)), type(type_)
    {
    }
};
