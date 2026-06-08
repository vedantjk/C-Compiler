#pragma once

#include "Expressions.h"
class TernaryExpr : public Expression
{
  public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> thenBranch;
    std::unique_ptr<Expression> elseBranch;

    TernaryExpr(std::unique_ptr<Expression> condition, std::unique_ptr<Expression> ifBranch,
                std::unique_ptr<Expression> elseBranch, int line, int col)
        : Expression(NodeKind::TernaryExpr, line, col), condition(std::move(condition)),
          thenBranch(std::move(ifBranch)), elseBranch(std::move(elseBranch))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::TernaryExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "( ";
        condition->print(out, tab);
        out << " ? ";
        thenBranch->print(out, tab);
        out << " : ";
        elseBranch->print(out, tab);
        out << " )";
    }
};
