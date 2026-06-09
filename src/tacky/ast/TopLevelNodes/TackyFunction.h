#pragma once
#include "../../../types/TypeQueries.h"
#include "../../instructions/instructions.h"
#include "../../instructions/val.h"
#include "./TackyTopLevelNode.h"

#include <string>
#include <utility>
#include <vector>

// A function parameter, self-typed: name + scalar width, plus a struct tag when
// the parameter is a by-value struct (so codegen can classify and slot it without
// a side-map). structTag is empty for scalar/pointer parameters.
struct TackyParam
{
    std::string name;
    ConstantType type;
    std::string structTag;
};

class TackyFunction : public TackyTopLevelNode
{
  public:
    std::string name;
    std::vector<std::unique_ptr<TackyInstruction>> instructions;
    // each parameter carries its name and width, so codegen sizes the prologue move
    std::vector<TackyParam> params;
    bool global;
    // System V return classification for a struct-returning function: a MEMORY
    // return takes a hidden destination pointer in RDI; a register return packs
    // its eightbytes into RAX/RDX/XMM0/XMM1.
    bool returnsStruct = false;
    StructABI returnABI;
    TackyFunction(const int line_, const int column_, std::string name_, bool global_,
                  std::vector<std::unique_ptr<TackyInstruction>> instructions_,
                  std::vector<TackyParam> params_)
        : TackyTopLevelNode(line_, column_), name(std::move(name_)), global(global_),
          instructions(std::move(instructions_)), params(std::move(params_)) {};
};
