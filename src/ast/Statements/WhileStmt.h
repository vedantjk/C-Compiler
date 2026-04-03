#pragma once

#include "BlockStmt.h"
#include "../Expressions/Expressions.h"
#include "Statement.h"
#include <memory>
#include <ostream>

class WhileStmt : public Statement{
    std::shared_ptr<Expression> condition;
    std::shared_ptr<BlockStmt> whileBlock;

public:

    WhileStmt(int line_, int col_, std::shared_ptr<Expression> condition_, std::shared_ptr<BlockStmt> whileBlock_) : 
        Statement(line_, col_), condition(std::move(condition_)), whileBlock(std::move(whileBlock_)) {}

    void print(std::ostream& out, int tab) const override{
        out<<"WHILE ( " ;
        condition->print(out, tab);
        out<<" )\n";
        whileBlock->print(out, tab+1);
    }

};