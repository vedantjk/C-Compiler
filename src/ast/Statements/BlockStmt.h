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

    int returnStatementsSize() const{
        return statements.size();
    }

    void print(std::ostream &out, int tab) const override
    {

        for (auto &statement : statements)
        {
            statement->print(out, tab);
            out << "\n";
        }
    }
};