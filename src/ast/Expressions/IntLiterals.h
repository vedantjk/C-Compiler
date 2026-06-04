#pragma once

#include <utility>

#include "./Expressions.h"

class IntLiterals : public Expression
{
  public:
    unsigned long long value;
    std::shared_ptr<Type> type;
    IntLiterals(int line_, int col_, unsigned long long value_, std::shared_ptr<Type> type_)
        : Expression(line_, col_), value(value_), type(std::move(type_))
    {
    }

    void print(std::ostream &out, int tab) const override { out << value; }
};