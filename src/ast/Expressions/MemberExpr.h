#pragma once

#include "Expressions.h"
#include <iostream>
#include <memory>
#include <utility>

class MemberExpr : public Expression
{
  public:
    std::shared_ptr<Expression> object;
    std::string field;
    bool isArrow;

    MemberExpr(std::shared_ptr<Expression> object, std::string field, bool isArrow, int line,
               int col)
        : Expression(line, col), object(std::move(object)), field(std::move(field)),
          isArrow(isArrow)
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        object->print(out, tab);
        if (isArrow)
        {
            out << "->";
        }
        else
            out << ".";
        out << field;
    }
};
