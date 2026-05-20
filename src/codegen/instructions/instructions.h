#pragma once
#include "operand.h"

#include <memory>

class Instruction
{
    public:
    Instruction() = default;
    virtual ~Instruction() = default;
};

class MoveInstruction : public Instruction
{
    public:
    std::unique_ptr<Operand> src, dst;
    MoveInstruction(std::unique_ptr<Operand> src, std::unique_ptr<Operand> dst)
           : src(std::move(src)), dst(std::move(dst)) {}

};

class ReturnInstruction : public Instruction
{
    public:
    ReturnInstruction() = default;

};