#pragma once

#include "./TopLevelNode.h"
#include "../Statements/BlockStmt.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>
struct Parameter
{
    std::string type;
    std::string name;
    int line;
    int col;

    Parameter(std::string type_, std::string name_, int line_, int col_) : type(type_), name(name_), line(line_), col(col_) {}
};

class Function : public TopLevelNode
{
    std::string name;
    std::string type;
    std::vector<Parameter> parameters;
    std::shared_ptr<BlockStmt> statements;

    public:
    Function(int line_, int col_, std::string name_, std::string type_, std::vector<Parameter> parameters_, std::shared_ptr<BlockStmt> statements_) : 
        TopLevelNode(line_, col_), name(name_), type(type_), parameters(std::move(parameters_)), statements(std::move(statements_)){}

    void print(std::ostream& out, int tab) const override{
        out << type << " " << name << " ( ";
        for(int i = 0; i<(int)parameters.size(); i++){
            out << parameters[i].type << " " << parameters[i].name;
            if(i!=(int)parameters.size() - 1) out <<", "; 
        }
        out << " ) {\n";
        statements->print(out, tab+1);
        out << "} \n";
    }
};