#pragma once
#include "../../instructions/instructions.h"
#include "./TackyTopLevelNode.h"

#include <string>
#include <vector>

class TackyFunction : public TackyTopLevelNode
{
  public:
    std::string name;
    std::vector<std::unique_ptr<TackyInstruction>> instructions;
    std::vector<std::string> params;
    TackyFunction(const int line_, const int column_, std::string name_,
                  std::vector<std::unique_ptr<TackyInstruction>> instructions_,
                  std::vector<std::string> params_)
        : TackyTopLevelNode(line_, column_), name(std::move(name_)),
          instructions(std::move(instructions_)), params(std::move(params_)) {};
};
