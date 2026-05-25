#pragma once

#include "../../types/types.h"
#include "../Expressions/Expressions.h"
#include "./TopLevelNode.h"
#include <iostream>
#include <memory>
#include <string>

class VarDecl : public TopLevelNode
{
  public:
    std::string name;
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> initialization;
    bool global;

    VarDecl(int line_, int col_, std::string name_, std::shared_ptr<Type> type_,
            std::shared_ptr<Expression> initialization_, bool global = false)
        : TopLevelNode(line_, col_), name(name_), type(type_),
          initialization(std::move(initialization_)), global(global)
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        out << type->toString() << " " << name;
        if (initialization)
        {
            out << " = ";
            initialization->print(out, tab);
        }
    }
};