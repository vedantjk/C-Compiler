#pragma once
#include "../../utils/common.h"
#include "operand.h"
#include <memory>
#include <utility>
#include <vector>

enum class InstrKind
{
    MoveInstruction,
    ReturnInstruction,
    UnaryInstruction,
    BinaryInstruction,
    IDivInstruction,
    DivInstruction,
    CdqInstruction,
    CmpInstruction,
    JumpInstruction,
    JumpCCInstruction,
    SetCCInstruction,
    Label,
    PushInstruction,
    CallInstruction,
    MoveSXInstruction,
    MoveZeroExtendInstruction,
    CVTSI2SD,
    CVTTSD2SI,
    LeaInstruction,
    PopInstruction
};

class Instruction
{
  public:
    const InstrKind kind;
    explicit Instruction(InstrKind k) : kind(k) {}
    virtual ~Instruction() = default;
    InstrKind getKind() const { return kind; }
};

class MoveInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    AssemblyType type;
    MoveInstruction(std::unique_ptr<Operand> src, std::unique_ptr<Operand> dst,
                    const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::MoveInstruction), src(std::move(src)), dst(std::move(dst)),
          type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::MoveInstruction; }
};

class ReturnInstruction : public Instruction
{
  public:
    // Hardware registers holding the return value: AX/XMM0, or up to two for a struct
    // returned in registers; empty for a void return. Recorded so the register allocator's
    // liveness keeps the value live into the bare `ret`.
    std::vector<RegisterName> returnRegs;
    explicit ReturnInstruction(std::vector<RegisterName> returnRegs_ = {})
        : Instruction(InstrKind::ReturnInstruction), returnRegs(std::move(returnRegs_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::ReturnInstruction; }
};

class UnaryInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> operand;
    UnaryOp op;
    AssemblyType type;
    UnaryInstruction(std::unique_ptr<Operand> operand, const UnaryOp op,
                     const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::UnaryInstruction), operand(std::move(operand)), op(op), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::UnaryInstruction; }
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
        : Instruction(InstrKind::BinaryInstruction), src(std::move(src_)), dst(std::move(dst_)),
          op(op_), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::BinaryInstruction; }
};

class IDivInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> operand;
    std::unique_ptr<MoveInstruction> scratchRegisterInstruction;
    AssemblyType type;
    explicit IDivInstruction(std::unique_ptr<Operand> operand_,
                             const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::IDivInstruction), operand(std::move(operand_)), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::IDivInstruction; }
};

class DivInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> operand;
    std::unique_ptr<MoveInstruction> scratchRegisterInstruction;
    AssemblyType type;
    explicit DivInstruction(std::unique_ptr<Operand> operand_,
                            const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::DivInstruction), operand(std::move(operand_)), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::DivInstruction; }
};

class CdqInstruction : public Instruction
{
  public:
    AssemblyType type;
    explicit CdqInstruction(const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::CdqInstruction), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::CdqInstruction; }
};

class CmpInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> a, b;
    std::unique_ptr<MoveInstruction> preStackFixInstruction;
    AssemblyType type;
    CmpInstruction(std::unique_ptr<Operand> a_, std::unique_ptr<Operand> b_,
                   const AssemblyType type_ = AssemblyType::LONGWORD)
        : Instruction(InstrKind::CmpInstruction), a(std::move(a_)), b(std::move(b_)), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::CmpInstruction; }
};

class JumpInstruction : public Instruction
{
  public:
    std::string identifier;
    explicit JumpInstruction(std::string identifier_)
        : Instruction(InstrKind::JumpInstruction), identifier(std::move(identifier_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::JumpInstruction; }
};

class JumpCCInstruction : public Instruction
{
  public:
    CondCode condCode;
    std::string identifier;
    JumpCCInstruction(const CondCode condCode_, std::string identifier_)
        : Instruction(InstrKind::JumpCCInstruction), condCode(condCode_),
          identifier(std::move(identifier_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::JumpCCInstruction; }
};

class SetCCInstruction : public Instruction
{
  public:
    CondCode condCode;
    std::unique_ptr<Operand> a;
    SetCCInstruction(const CondCode condCode_, std::unique_ptr<Operand> a_)
        : Instruction(InstrKind::SetCCInstruction), condCode(condCode_), a(std::move(a_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::SetCCInstruction; }
};

class Label : public Instruction
{
  public:
    std::string identifier;
    explicit Label(std::string identifier_)
        : Instruction(InstrKind::Label), identifier(std::move(identifier_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::Label; }
};

class PushInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> a;
    explicit PushInstruction(std::unique_ptr<Operand> a_)
        : Instruction(InstrKind::PushInstruction), a(std::move(a_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::PushInstruction; }
};

class CallInstruction : public Instruction
{
  public:
    std::string identifier;
    // Hardware registers the call reads as arguments (the GP/SSE arg registers, plus the
    // hidden return pointer in DI and AL for a variadic call). Recorded so the register
    // allocator's liveness can see what a bare `call` uses.
    std::vector<RegisterName> argRegs;
    explicit CallInstruction(std::string identifier_, std::vector<RegisterName> argRegs_ = {})
        : Instruction(InstrKind::CallInstruction), identifier(std::move(identifier_)),
          argRegs(std::move(argRegs_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::CallInstruction; }
};

// Sign-extend src into dst. srcType/dstType give the widths so the emitter picks
// the right mnemonic (movsbl byte->long, movsbq byte->quad, movslq long->quad).
class MoveSXInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    AssemblyType srcType, dstType;
    MoveSXInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_,
                      const AssemblyType srcType_ = AssemblyType::LONGWORD,
                      const AssemblyType dstType_ = AssemblyType::QUADWORD)
        : Instruction(InstrKind::MoveSXInstruction), src(std::move(src_)), dst(std::move(dst_)),
          srcType(srcType_), dstType(dstType_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::MoveSXInstruction; }
};

// Zero-extend src into dst. A byte source uses a real movzbl/movzbq; a longword
// source is lowered to a plain mov (a 32-bit write clears the upper half) by the
// fixup pass, so the emitter only ever sees the byte form.
class MoveZeroExtendInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    AssemblyType srcType, dstType;
    MoveZeroExtendInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_,
                              const AssemblyType srcType_ = AssemblyType::LONGWORD,
                              const AssemblyType dstType_ = AssemblyType::QUADWORD)
        : Instruction(InstrKind::MoveZeroExtendInstruction), src(std::move(src_)),
          dst(std::move(dst_)), srcType(srcType_), dstType(dstType_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::MoveZeroExtendInstruction; }
};

// double -> signed integer, truncating toward zero. `type` is the DESTINATION
// integer's width (LONGWORD or QUADWORD); the source is always an 8-byte SSE reg.
class CVTTSD2SI : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    AssemblyType type;
    CVTTSD2SI(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_,
              const AssemblyType type_)
        : Instruction(InstrKind::CVTTSD2SI), src(std::move(src_)), dst(std::move(dst_)), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::CVTTSD2SI; }
};

// signed integer -> double. `type` is the SOURCE integer's width (LONGWORD or
// QUADWORD); the destination is always an 8-byte SSE reg.
class CVTSI2SD : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    AssemblyType type;
    CVTSI2SD(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_, const AssemblyType type_)
        : Instruction(InstrKind::CVTSI2SD), src(std::move(src_)), dst(std::move(dst_)), type(type_)
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::CVTSI2SD; }
};

class LeaInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> src, dst;
    LeaInstruction(std::unique_ptr<Operand> src_, std::unique_ptr<Operand> dst_)
        : Instruction(InstrKind::LeaInstruction), src(std::move(src_)), dst(std::move(dst_))
    {
    }
    static bool classof(InstrKind k) { return k == InstrKind::LeaInstruction; }
};

class PopInstruction : public Instruction
{
  public:
    std::unique_ptr<Operand> reg;
    explicit PopInstruction(std::unique_ptr<Operand> reg_)
        : Instruction(InstrKind::PopInstruction), reg(std::move(reg_))
    {
    }

    static bool classof(InstrKind k) { return k == InstrKind::PopInstruction; }
};

// Apply `fn` to each operand slot an instruction owns (its std::unique_ptr<Operand>&
// fields). Instructions with no operand slots — returns, jumps, labels, calls, cdq — are
// simply skipped. A free function (not tied to the codegen driver) so both codegen and
// the register allocator can walk operands without depending on each other.
template <class F> void forEachSlot(Instruction &instruction, F &&fn)
{
    if (auto *p = dynamic_cast<MoveInstruction *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<MoveSXInstruction *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<MoveZeroExtendInstruction *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<UnaryInstruction *>(&instruction))
    {
        fn(p->operand);
    }
    else if (auto *p = dynamic_cast<BinaryInstruction *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<IDivInstruction *>(&instruction))
    {
        fn(p->operand);
    }
    else if (auto *p = dynamic_cast<DivInstruction *>(&instruction))
    {
        fn(p->operand);
    }
    else if (auto *p = dynamic_cast<CmpInstruction *>(&instruction))
    {
        fn(p->a);
        fn(p->b);
    }
    else if (auto *p = dynamic_cast<SetCCInstruction *>(&instruction))
    {
        fn(p->a);
    }
    else if (auto *p = dynamic_cast<PushInstruction *>(&instruction))
    {
        fn(p->a);
    }
    else if (auto *p = dynamic_cast<CVTSI2SD *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<CVTTSD2SI *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
    else if (auto *p = dynamic_cast<LeaInstruction *>(&instruction))
    {
        fn(p->src);
        fn(p->dst);
    }
}