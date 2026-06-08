#pragma once

#include "Expressions.h"
#include <iostream>
#include <memory>

class UnaryExpr : public Expression
{
  public:
    std::string op;
    std::shared_ptr<Expression> operand;
    bool isPostFix;

    UnaryExpr(int line_, int col_, std::string op_, std::shared_ptr<Expression> operand_,
              bool isPostFix_ = false)
        : Expression(NodeKind::UnaryExpr, line_, col_), op(op_), operand(operand_),
          isPostFix(isPostFix_)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::UnaryExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "(";
        if (!isPostFix)
            out << op;
        operand->print(out, tab);
        if (isPostFix)
            out << op;
        out << ")";
    }
};
