#pragma once

#include "./Expressions.h"
#include <memory>

class BinaryExpr : public Expression{
public:
    std::shared_ptr<Expression> left;
    std::shared_ptr<Expression> right;
    std::string binaryOp;

    BinaryExpr(int line_, int col_, std::shared_ptr<Expression> left_, std::shared_ptr<Expression> right_, std::string binaryOp_) :
        Expression(line_, col_), left(std::move(left_)), right(std::move(right_)), binaryOp(binaryOp_) {}

    void print(std::ostream& out, int tab) const override {
        out << "(";
        left->print(out, tab);
        out<<" "<<binaryOp<<" ";
        right->print(out, tab);
        out << ")";
    }
};