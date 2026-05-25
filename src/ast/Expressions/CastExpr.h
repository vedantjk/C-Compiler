#pragma once

#include "Expressions.h"
#include "types/types.h"

class CastExpr : public Expression
{
  public:
    std::shared_ptr<Type> type;
    std::shared_ptr<Expression> operand;

    CastExpr(std::shared_ptr<Type> type, std::shared_ptr<Expression> operand, int line, int column)
        : Expression(line, column), type(type), operand(operand) {};

    void print(std::ostream &out, int tab) const override
    {
        out << "(" << type->toString() << ")";
        operand->print(out, tab);
    }
};
