#pragma once
#include "./codegenTopLevelNode.h"
#include "../../instructions/instructions.h"

#include <vector>

class codegenFunction : public codegenTopLevelNode
{
    public:
    std::string name;
    std::vector<std::unique_ptr<Instruction>> instructions;
    codegenFunction(const int line_, const int column_, const std::string name_, std::vector<std::unique_ptr<Instruction>> instructions_) :
    codegenTopLevelNode(line_, column_), name(std::move(name_)), instructions(std::move(instructions_)) {};
};