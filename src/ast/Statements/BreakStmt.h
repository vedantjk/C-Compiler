#pragma once

#include "Statement.h"
#include <ostream>

class BreakStmt : public Statement
{

  public:
    BreakStmt(int line_, int col_) : Statement(line_, col_) {}

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "break;";
    }
};