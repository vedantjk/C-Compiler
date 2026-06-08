#pragma once
#include "BlockStmt.h"
#include "Statement.h"
#include "ast/Expressions/Expressions.h"

class DoWhileStmt : public Statement
{
  public:
    int label;
    std::unique_ptr<BlockStmt> block;
    std::unique_ptr<Expression> condition;

    DoWhileStmt(std::unique_ptr<BlockStmt> block, std::unique_ptr<Expression> condition, int line,
                int column)
        : Statement(NodeKind::DoWhileStmt, line, column), block(std::move(block)),
          condition(std::move(condition))
    {
    }

    static bool classof(NodeKind k) { return k == NodeKind::DoWhileStmt; }

    void print(std::ostream &out, int tab) const override
    {
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "do{\n";
        block->print(out, tab + 1);
        for (int i = 0; i < tab; i++)
        {
            out << "  ";
        }
        out << "}while(";
        condition->print(out, tab + 1);
        out << ");\n";
    }
};
