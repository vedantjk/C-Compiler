#pragma once

#include <utility>

#include "./Expressions.h"

class IntLiterals : public Expression
{
  public:
    unsigned long long value;
    std::shared_ptr<Type> type;
    IntLiterals(int line_, int col_, unsigned long long value_, std::shared_ptr<Type> type_)
        : Expression(NodeKind::IntLiterals, line_, col_), value(value_), type(std::move(type_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::IntLiterals; }

    void print(std::ostream &out, int tab) const override { out << value; }
};
