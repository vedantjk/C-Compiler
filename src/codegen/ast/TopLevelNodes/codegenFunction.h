#pragma once
#include "../../instructions/instructions.h"
#include "./codegenTopLevelNode.h"

#include <vector>

class codegenFunction : public codegenTopLevelNode
{
  public:
    std::string name;
    std::unique_ptr<AllocateStack> stackAllocation;
    std::vector<std::unique_ptr<Instruction>> instructions;
    bool global;
    codegenFunction(const int line_, const int column_, const std::string name_, bool global_,
                    std::vector<std::unique_ptr<Instruction>> instructions_)
        : codegenTopLevelNode(line_, column_), name(std::move(name_)), global(global_),
          instructions(std::move(instructions_)) {};
};