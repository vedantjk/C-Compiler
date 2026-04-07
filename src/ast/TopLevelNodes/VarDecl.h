#pragma once

#include "./TopLevelNode.h"
#include "../Expressions/Expressions.h"
#include <string>
#include <memory>
#include <iostream>

class VarDecl : public TopLevelNode
{
    std::string name;
    std::string type;
    std::shared_ptr<Expression> initialization;

    public:
    VarDecl(int line_, int col_, std::string name_, std::string type_, std::shared_ptr<Expression> initialization_): 
        TopLevelNode(line_, col_), name(name_), type(type_),
            initialization(std::move(initialization_))
      {
      }

    void print(std::ostream &out, int tab) const override
      {
        out << type << " " << name;
        if(initialization){ 
          out<<" = ";
          initialization->print(out, tab);
        }
      }
};