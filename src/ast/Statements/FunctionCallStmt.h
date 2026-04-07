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

    FunctionCallStmt(int line_, int col_, std::string name_, std::vector<std::shared_ptr<Expression>> parameters_) : Statement(line_, col_), name(name_), parameters(parameters_) {}
    
    void print(std::ostream& out, int tab) const override{
        out<<name<<"(";
        for(auto& parameter : parameters){
            parameter->print(out, tab);
        }
        out<<")\n";
    }
};