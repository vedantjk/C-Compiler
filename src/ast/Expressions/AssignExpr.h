#pragma once

#include "Expressions.h"
#include <iostream>
class AssignExpr : public Expression
{
public:
    std::shared_ptr<Expression> lhs, rhs;
    std::string op;

    AssignExpr(std::shared_ptr<Expression> lhs, std::shared_ptr<Expression> rhs, std::string op, int line, int col) :
    Expression(line, col), lhs(std::move(lhs)), rhs(std::move(rhs)), op(op) {};

    void print(std::ostream &out, int tab) const override
    {
        lhs->print(out, tab);
        out<<" "<< op <<" (";
        rhs->print(out, tab);
        out <<")";
    }
};