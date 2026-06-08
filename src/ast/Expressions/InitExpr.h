#pragma once
#include "Expressions.h"

#include <vector>
class InitExpr : public Expression
{
  public:
    std::vector<std::unique_ptr<Expression>> elements;

    InitExpr(std::vector<std::unique_ptr<Expression>> elements, int line, int col)
        : Expression(NodeKind::InitExpr, line, col), elements(std::move(elements))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::InitExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "{";
        for (int i = 0; i < static_cast<int>(elements.size()); i++)
        {
            elements[i]->print(out, tab);
            if (i != static_cast<int>(elements.size() - 1))
                out << ",";
        }

        out << "}";
    }
};
