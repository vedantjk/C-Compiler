#pragma once

#include "Statement.h"
#include <ostream>

class ContinueStmt : public Statement
{

  public:
    int label;
    ContinueStmt(int line_, int col_) : Statement(NodeKind::ContinueStmt, line_, col_) {}

    static bool classof(NodeKind k) { return k == NodeKind::ContinueStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "continue;";
    }
};
