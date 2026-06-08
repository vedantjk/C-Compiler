#pragma once

#include "./Statement.h"
#include <iostream>
#include <memory>
#include <vector>

class BlockStmt : public Statement
{
  public:
    std::vector<std::unique_ptr<Statement>> statements;

    BlockStmt(int line_, int col_, std::vector<std::unique_ptr<Statement>> statements_)
        : Statement(NodeKind::BlockStmt, line_, col_), statements(std::move(statements_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::BlockStmt; }

    int returnStatementsSize() const { return statements.size(); }

    void print(std::ostream &out, int tab) const override
    {

        for (auto &statement : statements)
        {
            statement->print(out, tab);
            out << "\n";
        }
    }
};
