#pragma once

#include "Statement.h"
#include <ostream>

class ContinueStmt : public Statement
{

  public:
    int label;
    ContinueStmt(int line_, int col_) : Statement(line_, col_) {}

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "continue;";
    }
};