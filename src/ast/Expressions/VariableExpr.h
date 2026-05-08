#pragma once

#include "./Expressions.h"
#include <ostream>

class VariableExpr : public Expression{
public:
    std::string name;

    VariableExpr(int line_, int col_, std::string name_) : Expression(line_, col_), name(name_) {}

    void print(std::ostream& out, int tab) const override {
        out << name ;
    }
}; 