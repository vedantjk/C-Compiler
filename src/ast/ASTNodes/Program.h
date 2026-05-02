#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include "./ASTNode.h"
#include "../TopLevelNodes/TopLevelNode.h"

class Program : public ASTNode
{
    std::vector<std::shared_ptr<TopLevelNode>> nodes;

    public:
    explicit Program(std::vector<std::shared_ptr<TopLevelNode>> nodes) : ASTNode(0,0) ,
nodes(std::move(nodes)) {}

    void print(std::ostream& out, int tab) const override{
        for(auto& node : nodes){
            node->print(out, tab);
        }
    }
};