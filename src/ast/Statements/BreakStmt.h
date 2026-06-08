#pragma once

#include "Statement.h"
#include <ostream>

class BreakStmt : public Statement
{

  public:
    int label;
    BreakStmt(int line_, int col_) : Statement(NodeKind::BreakStmt, line_, col_) {}

    static bool classof(NodeKind k) { return k == NodeKind::BreakStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "break;";
    }
};
