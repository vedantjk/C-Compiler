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
        out << "DECLARE STMT\n";
        for (auto& var : variables)
        {
            var->print(out, tab);
        }
    }
};
