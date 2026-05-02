#pragma once
#include "BlockStmt.h"
#include "Statement.h"
#include "ast/Expressions/Expressions.h"

class DoWhileStmt : public Statement
{
    std::shared_ptr<BlockStmt> block;
    std::shared_ptr<Expression> condition;

    public:

    DoWhileStmt(std::shared_ptr<BlockStmt> block, std::shared_ptr<Expression> condition, int line, int column) :
    Statement(line, column), block(std::move(block)), condition(std::move(condition)) {}

    void print(std::ostream &out, int tab) const override
    {
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        out << "do{\n";
        block->print(out, tab+1);
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        out << "}while(";
        condition->print(out, tab+1);
        out<<");\n";
    }
};

