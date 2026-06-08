#pragma once
#include "../../../types/StructLayout.h"
#include "../TopLevelNodes/TackyTopLevelNode.h"
#include "./TackyASTNode.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Footprint of an aggregate (array) object, which a self-typed value can't carry;
// codegen needs it to size and align the stack slot.
struct ArrayObject
{
    long long size;
    int align;
};

class TackyProgram : public TackyASTNode
{
  public:
    std::vector<std::unique_ptr<TackyTopLevelNode>> nodes;
    // Every static-storage variable name (file-scope, block static, and extern),
    // so codegen addresses them RIP-relative even when no definition is emitted.
    std::unordered_set<std::string> staticNames;
    // Array objects (by unique name) that need a sized, aligned stack slot.
    std::unordered_map<std::string, ArrayObject> arrayObjects;
    // Struct objects (by unique name): their sized/aligned slot plus the System V
    // eightbyte classification, so codegen can both lay out the slot and apply the
    // calling convention without re-deriving the layout.
    std::unordered_map<std::string, StructABI> structObjects;
    TackyProgram(const int line_, const int column_,
                 std::vector<std::unique_ptr<TackyTopLevelNode>> nodes_)
        : TackyASTNode(line_, column_), nodes(std::move(nodes_)) {};
};
