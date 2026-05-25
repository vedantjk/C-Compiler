#pragma once

#include "./Expressions.h"

class StringLiterals : public Expression
{
  public:
    std::string literal;

    StringLiterals(int line_, int col_, std::string literal_)
        : Expression(line_, col_), literal(literal_)
    {
    }

    void print(std::ostream &out, int tab) const override { out << literal; }
};