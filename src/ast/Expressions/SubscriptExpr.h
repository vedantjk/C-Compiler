#pragma once

#include "Expressions.h"

#include <iostream>
#include <memory>
#include <utility>

class SubscriptExpr : public Expression
{
  public:
    std::shared_ptr<Expression> lvalue;
    std::shared_ptr<Expression> index;

    SubscriptExpr(std::shared_ptr<Expression> lvalue, std::shared_ptr<Expression> index, int line,
                  int col)
        : Expression(line, col), lvalue(std::move(lvalue)), index(std::move(index)) {};

    void print(std::ostream &out, int tab) const override
    {
        lvalue->print(out, tab);
        out << "[";
        index->print(out, tab);
        out << "]";
    }
};
