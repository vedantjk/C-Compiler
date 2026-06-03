#pragma once
#include "../../instructions/instructions.h"
#include "../../instructions/val.h"
#include "./TackyTopLevelNode.h"

#include <string>
#include <utility>
#include <vector>

class TackyFunction : public TackyTopLevelNode
{
  public:
    std::string name;
    std::vector<std::unique_ptr<TackyInstruction>> instructions;
    // each parameter carries its name and width, so codegen sizes the prologue move
    std::vector<std::pair<std::string, ConstantType>> params;
    bool global;
    TackyFunction(const int line_, const int column_, std::string name_, bool global_,
                  std::vector<std::unique_ptr<TackyInstruction>> instructions_,
                  std::vector<std::pair<std::string, ConstantType>> params_)
        : TackyTopLevelNode(line_, column_), name(std::move(name_)), global(global_),
          instructions(std::move(instructions_)), params(std::move(params_)) {};
};
