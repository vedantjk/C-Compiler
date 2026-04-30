#pragma once

#include "./Statement.h"
#include "../TopLevelNodes/VarDecl.h"
#include <vector>
#include <iostream>

class DeclareStmt : public Statement
{
    std::vector<std::shared_ptr<VarDecl>> variables;

    public:
    DeclareStmt(int line_, int col_, std::vector<std::shared_ptr<VarDecl>> variables_)
        : Statement(line_, col_), variables(std::move(variables_))
    {
    }

    void print(std::ostream &out, int tab) const override { 
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        for (size_t i= 0; i<variables.size(); i++)
        {
            variables[i]->print(out, tab);
            if (i != variables.size() - 1) out<<",";
        }
        out << ";";
    }
};
