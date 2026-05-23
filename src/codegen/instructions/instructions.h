#pragma once
#include "operand.h"
#include "../../utils/common.h"
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

class UnaryInstruction : public Instruction
{
    public:
    std::unique_ptr<Operand> operand;
    UnaryOp op;
    UnaryInstruction(std::unique_ptr<Operand> operand, const UnaryOp op) : operand(std::move(operand)), op(op) {}
};

class AllocateStack : public Instruction
{
    public:
    int quantity;
    AllocateStack(const int quantity_) : quantity(quantity_) {}
};