#pragma once
#include "Expressions.h"

#include <vector>
class InitExpr : public Expression
{
public:
    std::vector<std::shared_ptr<Expression>> elements;

    InitExpr(std::vector<std::shared_ptr<Expression>> elements, int line, int col) : Expression(line,col), elements(elements) {}

    void print(std::ostream &out, int tab) const override
    {
        out<<"{";
        for (int i = 0; i < static_cast<int>(elements.size()); i++)
        {
            elements[i]->print(out, tab);
            if (i != static_cast<int>(elements.size()-1)) out <<",";
        }

        out<<"}";
    }
};