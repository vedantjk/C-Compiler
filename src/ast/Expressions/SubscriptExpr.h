#pragma once

#include "Expressions.h"

#include <iostream>
#include <memory>
#include <utility>

class SubscriptExpr : public Expression
{
  public:
    std::unique_ptr<Expression> lvalue;
    std::unique_ptr<Expression> index;

    SubscriptExpr(std::unique_ptr<Expression> lvalue, std::unique_ptr<Expression> index, int line,
                  int col)
        : Expression(NodeKind::SubscriptExpr, line, col), lvalue(std::move(lvalue)),
          index(std::move(index)) {};

    static bool classof(NodeKind k) { return k == NodeKind::SubscriptExpr; }

    void print(std::ostream &out, int tab) const override
    {
        lvalue->print(out, tab);
        out << "[";
        index->print(out, tab);
        out << "]";
    }
};
