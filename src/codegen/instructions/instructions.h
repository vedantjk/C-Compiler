#pragma once
#include "../../utils/common.h"
#include "operand.h"
#include <memory>
#include <utility>

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
    AssemblyType type;
    MoveInstruction(std::unique_ptr<Operand> src, std::unique_ptr<Operand> dst,
                    const AssemblyType type_ = AssemblyType::LONGWORD)
        : src(std::move(src)), dst(std::move(dst)), type(type_)
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
    AssemblyType type;
    UnaryInstruction(std::unique_ptr<Operand> operand, const UnaryOp op,
                     const AssemblyType type_ = AssemblyType::LONGWORD)
        : operand(std::move(operand)), op(op), type(type_)
    {
    }
};

class BinaryInstruction : public Instruction
{
  public:
    std::unique_ptr<MoveInstruction> preStackFixInstruction;
    std::unique_ptr<MoveInstruction> postStackFixInstruction;
    std::unique_ptr<Operand> src, dst;
    BinaryOp op;
    AssemblyType type;
    BinaryInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_,
                      const BinaryOp op_, const AssemblyType type_ = AssemblyType::LONGWORD)
        : src(std::move(src_)), dst(std::move(dst_)), op(op_), type(type_)
    {
    }
};

class IDivInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> operand;
    std::unique_ptr<MoveInstruction> scratchRegisterInstruction;
    AssemblyType type;
    explicit IDivInstruction(std::unique_ptr<Operand> operand_,
                             const AssemblyType type_ = AssemblyType::LONGWORD)
        : operand(std::move(operand_)), type(type_)
    {
    }
};

class CdqInstruction : public Instruction
{
  public:
    AssemblyType type;
    explicit CdqInstruction(const AssemblyType type_ = AssemblyType::LONGWORD) : type(type_) {}
};

class CmpInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> a, b;
    std::unique_ptr<MoveInstruction> preStackFixInstruction;
    AssemblyType type;
    CmpInstruction(std::unique_ptr<Operand> a_, std::unique_ptr<Operand> b_,
                   const AssemblyType type_ = AssemblyType::LONGWORD)
        : a(std::move(a_)), b(std::move(b_)), type(type_)
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

class PushInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> a;
    explicit PushInstruction(std::unique_ptr<Operand> a_) : a(std::move(a_)) {}
};

class CallInstruction : public Instruction
{
  public:
    std::string identifier;
    explicit CallInstruction(std::string identifier_) : identifier(std::move(identifier_)) {}
};

class MoveSXInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    MoveSXInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_)
        : src(std::move(src_)), dst(std::move(dst_))
    {
    }
};