#pragma once
#include "../TopLevelNodes/TackyTopLevelNode.h"
#include "./TackyASTNode.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class TackyProgram : public TackyASTNode
{
  public:
    std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
    // Every static-storage variable name (file-scope, block static, and extern),
    // so codegen addresses them RIP-relative even when no definition is emitted.
    std::unordered_set<std::string> staticNames;
    TackyProgram(const int line_, const int column_,
                 std::vector<std::unique_ptr<TackyTopLevelNode>> nodes_)
        : TackyASTNode(line_, column_), nodes(std::move(nodes_)) {};
};
