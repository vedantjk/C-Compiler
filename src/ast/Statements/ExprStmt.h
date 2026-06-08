#pragma once

#include "../Expressions/Expressions.h"
#include "Statement.h"

class ExprStmt : public Statement
{
  public:
    std::unique_ptr<Expression> expr;
    bool printSemiColon = true;

    ExprStmt(std::unique_ptr<Expression> expr, int line, int col, bool printSemiColon = true)
        : Statement(NodeKind::ExprStmt, line, col), expr(std::move(expr)),
          printSemiColon(printSemiColon)
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::ExprStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        expr->print(out, tab);
        if (printSemiColon)
            out << ";";
    }
};
