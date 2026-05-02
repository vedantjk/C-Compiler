#pragma once

#include "./TopLevelNode.h"
#include "../Expressions/Expressions.h"
#include "../../types/types.h"
#include <string>
#include <memory>
#include <iostream>

class VarDecl : public TopLevelNode
{
    std::string name;
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> initialization;
    bool global;

    public:
    VarDecl(int line_, int col_, std::string name_, std::shared_ptr<Type> type_, std::shared_ptr<Expression> initialization_, bool global = false):
        TopLevelNode(line_, col_), name(name_), type(type_),
            initialization(std::move(initialization_)), global(global)
      {
      }

    void print(std::ostream &out, int tab) const override
      {
        out << type->toString() << " " << name;
        if(initialization){ 
          out<<" = ";
          initialization->print(out, tab);
        }
        if (global)
        {
            out<<";\n";
        }
      }
};