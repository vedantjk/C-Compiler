#pragma once

#include "../TopLevelNodes/VarDecl.h"
#include "../TopLevelNodes/StructDecl.h"
#include "./Statement.h"
#include <iostream>
#include <variant>
#include <vector>

class DeclareStmt : public Statement
{
public:
    std::variant<std::vector<std::shared_ptr<VarDecl>>,std::shared_ptr<StructDecl>> variables;

    DeclareStmt(int line_, int col_, std::variant<std::vector<std::shared_ptr<VarDecl>>,
                               std::shared_ptr<StructDecl>> variables_)
        : Statement(line_, col_), variables(std::move(variables_))
    {
    }

    void print(std::ostream &out, int tab) const override { 
        for(int i = 0; i<tab;i++){
            out<<"  ";
        }
        if (std::holds_alternative<std::vector<std::shared_ptr<VarDecl>>>(variables))
        {
            auto& val = std::get<std::vector<std::shared_ptr<VarDecl>>>(variables);
            for (size_t i= 0; i<val.size(); i++)
            {
                val[i]->print(out, tab+1);
                if (i != val.size() - 1) out<<",";
            }
            out << ";";
        } else
        {
            auto& val = std::get<std::shared_ptr<StructDecl>>(variables);
            val->print(out, tab+1);
        }
    }
};
