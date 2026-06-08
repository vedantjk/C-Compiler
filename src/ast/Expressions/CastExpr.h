#pragma once

#include "Expressions.h"
#include "types/types.h"

class CastExpr : public Expression
{
  public:
    std::shared_ptr<Type> type;
    std::unique_ptr<Expression> operand;

    CastExpr(std::shared_ptr<Type> type, std::unique_ptr<Expression> operand, int line, int column)
        : Expression(NodeKind::CastExpr, line, column), type(type), operand(std::move(operand)) {};

    static bool classof(NodeKind k) { return k == NodeKind::CastExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "(" << type->toString() << ")";
        operand->print(out, tab);
    }
};
