#pragma once
#include "../../types/types.h"
#include "Expressions.h"

class SizeOfExpr : public Expression
{
  public:
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> expr;

    SizeOfExpr(int line, int col, std::shared_ptr<Type> type = nullptr,
               std::shared_ptr<Expression> expr = nullptr)
        : Expression(NodeKind::SizeOfExpr, line, col), type(std::move(type)), expr(std::move(expr))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::SizeOfExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "sizeof(";
        if (type)
            out << type->toString();
        if (expr)
            expr->print(out, tab);
        out << ")";
    }
};
