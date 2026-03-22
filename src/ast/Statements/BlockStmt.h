#pragma once

#include "./Statement.h"
#include <memory>
#include <vector>
#include <iostream>

class BlockStmt : public Statement
{
    std::vector<std::shared_ptr<Statement>> statements;

    public:
    BlockStmt(int line_, int col_, std::vector<std::shared_ptr<Statement>> statements_)
        : Statement(line_, col_), statements(std::move(statements_))
    {
    }

    void print(std::ostream &out, int tab) const override
    {
        out << "BlockStmt\n";
        for (auto &statement : statements)
        {
            statement->print(out, tab);
        }
    }
};