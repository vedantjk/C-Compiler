#pragma once

#include "Statement.h"
#include "../Expressions/Expressions.h"

class ExprStmt : public Statement
{
public:
    std::shared_ptr<Expression> expr;
    bool printSemiColon = true;

    ExprStmt(std::shared_ptr<Expression> expr, int line, int col, bool printSemiColon = true) :
    Statement(line, col), expr(std::move(expr)), printSemiColon(printSemiColon) {}

    void print(std::ostream &out, int tab) const override
    {
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        expr->print(out, tab);
        if (printSemiColon) out << ";";
    }
};