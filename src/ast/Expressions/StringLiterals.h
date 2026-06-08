#pragma once

#include "./Expressions.h"

class StringLiterals : public Expression
{
  public:
    std::string literal;

    StringLiterals(int line_, int col_, std::string literal_)
        : Expression(NodeKind::StringLiterals, line_, col_), literal(literal_)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::StringLiterals; }

    void print(std::ostream &out, int tab) const override { out << literal; }
};
