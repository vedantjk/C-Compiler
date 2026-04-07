#pragma once

#include "Statement.h"
#include "../Expressions/Expressions.h"
#include <ostream>
#include <vector>
#include <memory>
#include <iostream>

class FunctionCallStmt : public Statement{
    std::string name;
    std::vector<std::shared_ptr<Expression>> parameters;

    public:

    FunctionCallStmt(int line_, int col_, std::string name_, std::vector<std::shared_ptr<Expression>> parameters_) : Statement(line_, col_), name(name_), parameters(std::move(parameters_)) {}
    
    void print(std::ostream& out, int tab) const override{
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        out<<name<<"(";
        for(int i = 0; i<(int)parameters.size(); i++){
            parameters[i]->print(out, tab);
            if(i!=(int)parameters.size() - 1) out << ", ";
        }
        out<<");";
    }
};