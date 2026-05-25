#pragma once

#include "../Expressions/Expressions.h"
#include "./Statement.h"
#include <iostream>
#include <memory>
class ReturnStmt : public Statement
{
  public:
    std::shared_ptr<Expression> returnExpression;

    ReturnStmt(int line_, int col_, std::shared_ptr<Expression> returnExpression_)
        : Statement(line_, col_), returnExpression(std::move(returnExpression_))
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "return ";
        if (returnExpression)
            returnExpression->print(out, tab);
        out << ";";
    }
};