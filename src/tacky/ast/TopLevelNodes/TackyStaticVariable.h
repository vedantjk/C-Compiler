#pragma once
#include "../../instructions/val.h"
#include "TackyTopLevelNode.h"
#include <string>

class TackyStaticVariable : public TackyTopLevelNode
{
  public:
    std::string identifier;
    bool global;
    long long init;
    ConstantType type; // declared width of the object: emit .long (INT) or .quad (LONG)
    TackyStaticVariable(int line_, int col_, std::string identifier_, bool global_, long long init_,
                        ConstantType type_)
        : TackyTopLevelNode(line_, col_), identifier(std::move(identifier_)), global(global_),
          init(init_), type(type_)
    {
    }
};
