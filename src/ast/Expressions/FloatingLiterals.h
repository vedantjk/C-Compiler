#pragma once

#include <utility>

#include "./Expressions.h"

class FloatingLiterals : public Expression
{
  public:
    double value;
    std::shared_ptr<Type> type;
    FloatingLiterals(int line_, int col_, double value_, std::shared_ptr<Type> type_)
        : Expression(line_, col_), value(value_), type(std::move(type_))
    {
    }

    void print(std::ostream &out, int tab) const override { out << value; }
};