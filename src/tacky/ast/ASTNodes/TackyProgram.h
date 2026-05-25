#pragma once
#include "../TopLevelNodes/TackyTopLevelNode.h"
#include "./TackyASTNode.h"

#include <memory>
#include <vector>

class TackyProgram : public TackyASTNode
{
  public:
    std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
    TackyProgram(const int line_, const int column_,
                 std::vector<std::unique_ptr<TackyTopLevelNode>> nodes_)
        : TackyASTNode(line_, column_), nodes(std::move(nodes_)) {};
};
