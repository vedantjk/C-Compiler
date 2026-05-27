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
    std::shared_ptr<Statement> initialization;
    std::shared_ptr<Statement> condition;
    std::shared_ptr<Statement> update;
    std::shared_ptr<BlockStmt> forBlock;

    ForStmt(int line_, int col_, std::shared_ptr<Statement> initialization_,
            std::shared_ptr<Statement> condition_, std::shared_ptr<Statement> update_,
            std::shared_ptr<BlockStmt> forBlock_)
        : Statement(line_, col_), initialization(std::move(initialization_)),
          condition(std::move(condition_)), update(std::move(update_)),
          forBlock(std::move(forBlock_))
    {
    }

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