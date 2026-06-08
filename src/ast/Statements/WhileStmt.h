#pragma once

#include "../Expressions/Expressions.h"
#include "BlockStmt.h"
#include "Statement.h"
#include <memory>
#include <ostream>

class WhileStmt : public Statement
{
  public:
    int label;
    std::shared_ptr<Expression> condition;
    std::shared_ptr<BlockStmt> whileBlock;

    WhileStmt(int line_, int col_, std::shared_ptr<Expression> condition_,
              std::shared_ptr<BlockStmt> whileBlock_)
        : Statement(NodeKind::WhileStmt, line_, col_), condition(std::move(condition_)),
          whileBlock(std::move(whileBlock_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::WhileStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "while ( ";
        condition->print(out, tab);
        out << " ){\n";
        whileBlock->print(out, tab + 1);
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "}";
    }
};
