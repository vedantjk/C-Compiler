#pragma once
#include "../TopLevelNodes/TackyTopLevelNode.h"
#include "./TackyASTNode.h"

#include <memory>
#include <vector>

class TackyProgram : public TackyASTNode
{
  public:
    std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
    // Object footprint and static-storage facts are no longer carried in side-maps:
    // every TackyVar / pseudo-operand is self-typed (isStatic / objSize / objAlign /
    // structTag), so codegen reads them off the operands during slot lowering.
    TackyProgram(const int line_, const int column_,
                 std::vector<std::unique_ptr<TackyTopLevelNode>> nodes_)
        : TackyASTNode(line_, column_), nodes(std::move(nodes_)) {};
};
