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
    std::string name;
    int line;
    int col;

    Parameter(std::string name_, int line_, int col_) : name(name_), line(line_), col(col_) {}
};

class Function : public TopLevelNode
{
    std::string name;
    std::string type;
    std::vector<Parameter> parameters;
    std::shared_ptr<BlockStmt> statements;

    public:
    Function(int line_, int col_, std::string name_, std::string type_, std::vector<Parameter> parameters_, std::shared_ptr<BlockStmt> statements_) : 
        TopLevelNode(line_, col_), name(name_), type(type_), parameters(parameters_), statements(std::move(statements_)){}

    void print(std::ostream& out, int tab) const override{
        out << name << " " << type << "\n";
        statements->print(out, tab);
    }
};