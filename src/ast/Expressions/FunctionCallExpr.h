#pragma once

#include "Expressions.h"
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

class FunctionCallExpr : public Expression{
    std::string functionName;
    std::vector<std::shared_ptr<Expression>> parameters;

public:

    FunctionCallExpr(int line_, int col_, std::string name_, std::vector<std::shared_ptr<Expression>> parameters_) :
    Expression(line_, col_), functionName(name_), parameters(std::move(parameters_)) {}

    void print(std::ostream& out, int tab) const override {
        out<<functionName<<"(";
        for(auto& parameter : parameters){
            parameter->print(out, tab);
        }
        out<<")\n";
    }

};