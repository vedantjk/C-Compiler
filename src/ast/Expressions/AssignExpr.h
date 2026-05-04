#pragma once

#include "Expressions.h"
#include <iostream>
class AssignExpr : public Expression
{
    std::shared_ptr<Expression> lhs, rhs;
public:
    AssignExpr(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, int line, int col) : Expression(line, col), lhs(std::move(lhs)), rhs(std::move(rhs)) {};

    void print(std::ostream &out, int tab) const override
    {
        lhs->print(out, tab);
        out <<" = (";
        rhs->print(out, tab);
        out <<")";
    }
};