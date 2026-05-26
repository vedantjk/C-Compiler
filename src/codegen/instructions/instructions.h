#pragma once
#include "../../utils/common.h"
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
        : src(std::move(src)), dst(std::move(dst))
    {
    }
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
    UnaryInstruction(std::unique_ptr<Operand> operand, const UnaryOp op)
        : operand(std::move(operand)), op(op)
    {
    }
};

class AllocateStack : public Instruction
{
  public:
    int quantity;
    AllocateStack(const int quantity_) : quantity(quantity_) {}
};

class BinaryInstruction : public Instruction
{
  public:
    std::unique_ptr<MoveInstruction> preStackFixInstruction;
    std::unique_ptr<MoveInstruction> postStackFixInstruction;
    std::unique_ptr<Operand> src, dst;
    BinaryOp op;
    BinaryInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_,
                      const BinaryOp op_)
        : src(std::move(src_)), dst(std::move(dst_)), op(op_)
    {
    }
};

class IDivInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> operand;
    std::unique_ptr<MoveInstruction> scratchRegisterInstruction;
    explicit IDivInstruction(std::unique_ptr<Operand> operand_) : operand(std::move(operand_)) {}
};

class CdqInstruction : public Instruction
{
  public:
    explicit CdqInstruction() = default;
};

class CmpInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> a, b;
    std::unique_ptr<MoveInstruction> preStackFixInstruction;
    CmpInstruction(std::unique_ptr<Operand> a_, std::unique_ptr<Operand> b_)
        : a(std::move(a_)), b(std::move(b_))
    {
    }
};

class JumpInstruction : public Instruction
{
  public:
    std::string identifier;
    explicit JumpInstruction(std::string identifier_) : identifier(std::move(identifier_)) {}
};

class JumpCCInstruction : public Instruction
{
  public:
    CondCode condCode;
    std::string identifier;
    JumpCCInstruction(const CondCode condCode_, std::string identifier_)
        : condCode(condCode_), identifier(std::move(identifier_))
    {
    }
};

class SetCCInstruction : public Instruction
{
  public:
    CondCode condCode;
    std::unique_ptr<Operand> a;
    SetCCInstruction(const CondCode condCode_, std::unique_ptr<Operand> a_)
        : condCode(condCode_), a(std::move(a_))
    {
    }
};

class Label : public Instruction
{
  public:
    std::string identifier;
    explicit Label(std::string identifier_) : identifier(std::move(identifier_)) {}
};