#pragma once

#include "BlockStmt.h"
#include "../Expressions/Expressions.h"
#include "Statement.h"
#include <memory>
#include <ostream>

class WhileStmt : public Statement{
public:
    std::shared_ptr<Expression> condition;
    std::shared_ptr<BlockStmt> whileBlock;

    WhileStmt(int line_, int col_, std::shared_ptr<Expression> condition_, std::shared_ptr<BlockStmt> whileBlock_) :
        Statement(line_, col_), condition(std::move(condition_)), whileBlock(std::move(whileBlock_)) {}

    void print(std::ostream& out, int tab) const override{
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        out<<"while ( " ;
        condition->print(out, tab);
        out<<" ){\n";
        whileBlock->print(out, tab+1);
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        out<<"}";
    }

};