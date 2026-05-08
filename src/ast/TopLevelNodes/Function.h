#pragma once

#include "./TopLevelNode.h"
#include "../Statements/BlockStmt.h"
#include "../../types/types.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
struct Parameter
{
    std::shared_ptr<Type> type;
    std::string name;
    int line;
    int col;

    Parameter(std::shared_ptr<Type> type_, std::string name_, int line_, int col_) : type(std::move(type_)), name(std::move(name_)), line(line_), col(col_) {}
};

class Function : public TopLevelNode
{
public:
    std::string name;
    std::shared_ptr<Type> type;
    std::vector<Parameter> parameters;
    std::shared_ptr<BlockStmt> statements;
    bool variadic;

    Function(int line_, int col_, std::string name_, std::shared_ptr<Type> type_, std::vector<Parameter> parameters_, std::shared_ptr<BlockStmt> statements_, bool variadic = false) :
        TopLevelNode(line_, col_), name(std::move(name_)), type(std::move(type_)), parameters(std::move(parameters_)), statements(std::move(statements_)), variadic(variadic) {}

    void print(std::ostream& out, int tab) const override{
        out << type->toString() << " " << name << " ( ";
        for(int i = 0; i<(int)parameters.size(); i++){
            out << parameters[i].type->toString() << " " << parameters[i].name;
            if(i!=(int)parameters.size() - 1) out <<", "; 
        }
        if (variadic)
        {
            out <<", ... ";
        }
        out << " ) ";
        if (statements != nullptr)
        {
            out <<"{\n";
            statements->print(out, tab+1);
            out << "}";
        }else
        {
            out <<";";
        }

    }
};