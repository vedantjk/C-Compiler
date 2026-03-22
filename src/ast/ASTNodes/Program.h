#pragma once

#include <iostream>
#include <vector>
#include <memory>
#include "./ASTNode.h"
#include "../TopLevelNodes/Function.h"

class Program : public ASTNode
{
    std::vector<std::shared_ptr<Function>> functions;

    public:
    Program(std::vector<std::shared_ptr<Function>> functions) : ASTNode(0,0) ,                        
functions(std::move(functions)) {} 

    void print(std::ostream& out, int tab) const override{
        for(auto& function : functions){
            function->print(out, tab);
        }
    }
};