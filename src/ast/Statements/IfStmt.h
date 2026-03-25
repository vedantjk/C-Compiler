#include "BlockStmt.h"
#include "../Expressions/Expressions.h"
#include <memory>
#include <ostream>

class IfStmt : public Statement{
    std::shared_ptr<Expression> condition;
    std::shared_ptr<BlockStmt> thenBlock;
    std::shared_ptr<BlockStmt> elseBlock;

public:

    IfStmt(int line_, int col_, std::shared_ptr<Expression> condition_, std::shared_ptr<BlockStmt> thenBlock_, std::shared_ptr<BlockStmt> elseBlock_):
        Statement(line_, col_), condition(std::move(condition_)), thenBlock(std::move(thenBlock_)), elseBlock(std::move(elseBlock_)){}

    void print(std::ostream& out, int tab) const override{
        out<< " if ( ";
        condition->print(out, tab);
        out<<"){ \n";
        thenBlock->print(out, tab+1);
        out<<"}\n";
        if(elseBlock){
            out<<"else {";
            elseBlock->print(out, tab+1);
            out<<"}\n";
        }
    }
};