#pragma once

#include "../Expressions/Expressions.h"
#include "./BlockStmt.h"
#include "Statement.h"
#include <memory>
#include <ostream>

class ForStmt : public Statement
{
  public:
    int label;
    std::unique_ptr<Statement> initialization;
    std::unique_ptr<Statement> condition;
    std::unique_ptr<Statement> update;
    std::unique_ptr<BlockStmt> forBlock;

    ForStmt(int line_, int col_, std::unique_ptr<Statement> initialization_,
            std::unique_ptr<Statement> condition_, std::unique_ptr<Statement> update_,
            std::unique_ptr<BlockStmt> forBlock_)
        : Statement(NodeKind::ForStmt, line_, col_), initialization(std::move(initialization_)),
          condition(std::move(condition_)), update(std::move(update_)),
          forBlock(std::move(forBlock_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::ForStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "for( ";
        if (initialization)
            initialization->print(out, tab);
        else
            out << ";";
        if (condition)
            condition->print(out, tab);
        else
            out << ";";
        if (update)
            update->print(out, tab);
        out << "){\n";
        forBlock->print(out, tab + 1);
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "}";
    }
};
