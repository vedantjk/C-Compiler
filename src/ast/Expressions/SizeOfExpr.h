#pragma once
#include "Expressions.h"
#include "../../types/types.h"

class SizeOfExpr : public Expression
{
public:
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> expr;

    SizeOfExpr(int line, int col, std::shared_ptr<Type> type = nullptr, std::shared_ptr<Expression> expr = nullptr) : Expression(line, col), type(std::move(type)), expr(std::move(expr)) {}

    void print(std::ostream &out, int tab) const override
    {
        out<<"sizeof(";
        if (type) out<<type->toString();
        if (expr) expr->print(out, tab);
        out <<")";
    }
};
