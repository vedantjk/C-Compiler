#pragma once

#include "../Expressions/Expressions.h"
#include "Statement.h"
#include <memory>
#include <ostream>

class AssignStmt : public Statement{
    std::shared_ptr<Expression> lhs;
    std::shared_ptr<Expression> rhs;

public:
    AssignStmt(int line_, int col_, std::shared_ptr<Expression> lhs_, std::shared_ptr<Expression> rhs_) :
        Statement(line_, col_), lhs(std::move(lhs_)), rhs(std::move(rhs_)) {}

    void print(std::ostream& out, int tab) const override{
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        lhs->print(out, tab);
        out<<" = ";
        rhs->print(out, tab);
        out << ";";
    }
};