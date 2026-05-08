#pragma once

#include "./Expressions.h"

class IntLiterals : public Expression
{
public:
    std::string value;

    IntLiterals(int line_, int col_, std::string value_) : Expression(line_, col_), value(value_) {}

    void print(std::ostream& out, int tab) const override 
    { 
        out << value;
    }
};