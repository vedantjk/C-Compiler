#pragma once

#include "../Expressions/Expressions.h"
#include "BlockStmt.h"
#include <memory>
#include <ostream>

class IfStmt : public Statement
{
  public:
    std::shared_ptr<Expression> condition;
    std::shared_ptr<BlockStmt> thenBlock;
    std::shared_ptr<BlockStmt> elseBlock;

    IfStmt(int line_, int col_, std::shared_ptr<Expression> condition_,
           std::shared_ptr<BlockStmt> thenBlock_, std::shared_ptr<BlockStmt> elseBlock_)
        : Statement(NodeKind::IfStmt, line_, col_), condition(std::move(condition_)),
          thenBlock(std::move(thenBlock_)), elseBlock(std::move(elseBlock_))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::IfStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "if(";
        condition->print(out, tab);
        out << "){ \n";
        thenBlock->print(out, tab + 1);
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "} ";
        if (elseBlock)
        {
            out << "else {\n";
            elseBlock->print(out, tab + 1);
            for (int i = 0; i < tab; i++)
            {
                out << "  ";
            }
            out << "}";
        }
    }
};
