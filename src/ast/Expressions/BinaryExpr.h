#pragma once

#include "./Expressions.h"
#include <memory>

class BinaryExpr : public Expression
{
  public:
    std::unique_ptr<Expression> left;
    std::unique_ptr<Expression> right;
    std::string binaryOp;

    BinaryExpr(int line_, int col_, std::unique_ptr<Expression> left_,
               std::unique_ptr<Expression> right_, std::string binaryOp_)
        : Expression(NodeKind::BinaryExpr, line_, col_), left(std::move(left_)),
          right(std::move(right_)), binaryOp(binaryOp_)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::BinaryExpr; }

    void print(std::ostream &out, int tab) const override
    {
        out << "(";
        left->print(out, tab);
        out << " " << binaryOp << " ";
        right->print(out, tab);
        out << ")";
    }
};
