#pragma once
#include "./codegenASTNode.h"
#include "../TopLevelNodes/codegenTopLevelNode.h"
#include <memory>
#include <vector>

class codegenProgram : public codegenASTNode
{
    public:
    std::vector<std::unique_ptr<codegenTopLevelNode>> nodes;
    codegenProgram(const int line_, const int column_, std::vector<std::unique_ptr<codegenTopLevelNode>> nodes_)
        : codegenASTNode(line_, column_), nodes(std::move(nodes_)) {};
};

