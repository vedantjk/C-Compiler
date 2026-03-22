#pragma once

#include "./Statement.h"
#include "../Expressions/Expressions.h"
#include <iostream>
#include <memory>
class ReturnStmt : public Statement
{
    std::shared_ptr<Expression> returnExpression;

    public:
    ReturnStmt(int line_, int col_, std::shared_ptr<Expression> returnExpression_) : 
        Statement(line_, col_), returnExpression(std::move(returnExpression_)) {}

    void print(std::ostream& out, int tab) const override { 
        
        out << "Return Stmt\n";
        if(returnExpression)
            returnExpression->print(out, tab);
    }
};