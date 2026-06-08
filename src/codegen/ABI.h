#pragma once

#include "../tacky/instructions/val.h"
#include "../types/TypeQueries.h"
#include "instructions/instructions.h"
#include "instructions/operand.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ABI — abstract interface for calling-convention / ABI decisions.
//
// A single concrete implementation (SysVx86_64ABI) carries the System V
// AMD64 rules.  The codegen driver holds a const ABI& (injected at
// construction) and calls it instead of its former hardcoded logic.
//
// Vocabulary types Slot and Arg are defined here because they are the
// currency exchanged between computeSlots (ABI's job) and the emission
// code in the driver (driver's job).
// ---------------------------------------------------------------------------

// One 8-byte transfer unit of an argument/parameter.
struct Slot
{
    enum Where
    {
        GP_REG,
        SSE_REG,
        STACK
    } where;
    RegisterName reg; // for GP_REG / SSE_REG
    int width;        // bytes to move (scalars 1..8; struct eightbytes always 8)
    int objOff;       // byte offset within a struct argument (0 for scalars)
    int stackIndex;   // for STACK: index among stack slots
    bool scalar;      // scalar argument (source/dest is `val`, not a struct slot)
    TackyVal val{TackyConstant(0, ConstantType::INT)};
};

struct Arg
{
    bool isStruct = false;
    std::string name;                                  // struct object name
    StructABI abi;                                     // struct classification
    TackyVal val{TackyConstant(0, ConstantType::INT)}; // scalar value
    AssemblyType stype = AssemblyType::LONGWORD;       // scalar width
    bool sdouble = false;
};

// ---------------------------------------------------------------------------

struct ABI
{
    virtual ~ABI() = default;

    // The ordered GP argument registers (DI, SI, DX, CX, R8, R9).
    virtual const std::vector<RegisterName> &intArgRegs() const = 0;

    // The ordered SSE argument registers (XMM0..XMM7).
    virtual const std::vector<RegisterName> &sseArgRegs() const = 0;

    // Return registers for integer and SSE values.
    virtual RegisterName returnIntReg() const = 0;  // AX
    virtual RegisterName returnIntReg2() const = 0; // DX  (second eightbyte)
    virtual RegisterName returnSseReg() const = 0;  // XMM0
    virtual RegisterName returnSseReg2() const = 0; // XMM1

    // Hidden return-pointer register (DI in SysV — occupies the first GP slot).
    virtual RegisterName hiddenReturnPtrReg() const = 0;

    // Stack alignment in bytes (16 for SysV).
    virtual int stackAlignment() const = 0;

    // True when the struct must be returned via a hidden memory pointer.
    virtual bool returnsInMemory(const StructABI &sabi) const = 0;

    // Assign each argument to registers and/or the stack.
    // `returnsMemory` reserves the first GP register for the hidden return pointer.
    // `stackCount` is set to the number of stack slots used.
    virtual std::vector<std::vector<Slot>>
    computeSlots(const std::vector<Arg> &args, bool returnsMemory, int &stackCount) const = 0;

    // x86 encoding legalization pass: rewrite instructions that violate
    // encoding constraints (mem-to-mem, large immediates, SSE reg requirements,
    // shift-count in CX, etc.) by inserting scratch-register staging.
    virtual void legalize(std::vector<std::unique_ptr<Instruction>> &instrs) const = 0;
};

// ---------------------------------------------------------------------------
// SysVx86_64ABI — System V AMD64 ABI, AT&T / ELF x86-64.
// ---------------------------------------------------------------------------

struct SysVx86_64ABI : ABI
{
    const std::vector<RegisterName> &intArgRegs() const override { return intArgRegs_; }
    const std::vector<RegisterName> &sseArgRegs() const override { return sseArgRegs_; }

    RegisterName returnIntReg() const override { return RegisterName::AX; }
    RegisterName returnIntReg2() const override { return RegisterName::DX; }
    RegisterName returnSseReg() const override { return RegisterName::XMM0; }
    RegisterName returnSseReg2() const override { return RegisterName::XMM1; }
    RegisterName hiddenReturnPtrReg() const override { return RegisterName::DI; }

    int stackAlignment() const override { return 16; }

    bool returnsInMemory(const StructABI &sabi) const override { return sabi.inMemory; }

    // --- computeSlots -------------------------------------------------------
    // Assign each argument to registers and/or the stack per the System V rules:
    // an integer/SSE scalar takes one register of its bank (else the stack); a
    // <=16-byte struct takes one register per eightbyte if both fit (all-or-nothing,
    // else the whole struct goes on the stack); a >16-byte struct goes on the stack.
    // `returnsMemory` reserves RDI for the hidden return pointer.
    std::vector<std::vector<Slot>> computeSlots(const std::vector<Arg> &args, bool returnsMemory,
                                                int &stackCount) const override
    {
        int gp = returnsMemory ? 1 : 0, sse = 0, stk = 0;
        std::vector<std::vector<Slot>> out;
        for (const auto &a : args)
        {
            std::vector<Slot> slots;
            if (!a.isStruct)
            {
                if (a.sdouble)
                {
                    if (sse < 8)
                        slots.push_back({Slot::SSE_REG, sseArgRegs_[sse++], 8, 0, 0, true, a.val});
                    else
                        slots.push_back({Slot::STACK, RegisterName::AX, 8, 0, stk++, true, a.val});
                }
                else if (gp < 6)
                    slots.push_back(
                        {Slot::GP_REG, intArgRegs_[gp++], bytesOf(a.stype), 0, 0, true, a.val});
                else
                    slots.push_back(
                        {Slot::STACK, RegisterName::AX, bytesOf(a.stype), 0, stk++, true, a.val});
                out.push_back(std::move(slots));
                continue;
            }

            const int ebCount = static_cast<int>((a.abi.size + 7) / 8);
            auto ebWidth = [&](int i)
            { return static_cast<int>(std::min<long long>(8, a.abi.size - 8 * i)); };
            auto onStack = [&]()
            {
                for (int i = 0; i < ebCount; i++)
                    slots.push_back(
                        {Slot::STACK, RegisterName::AX, ebWidth(i), 8 * i, stk++, false});
            };
            if (a.abi.inMemory)
            {
                onStack();
            }
            else
            {
                int needGP = 0, needSSE = 0;
                for (auto c : a.abi.classes)
                    (c == Eightbyte::Integer ? needGP : needSSE)++;
                if (gp + needGP <= 6 && sse + needSSE <= 8)
                {
                    for (int i = 0; i < static_cast<int>(a.abi.classes.size()); i++)
                    {
                        if (a.abi.classes[i] == Eightbyte::SSE)
                            slots.push_back(
                                {Slot::SSE_REG, sseArgRegs_[sse++], 8, 8 * i, 0, false});
                        else
                            slots.push_back(
                                {Slot::GP_REG, intArgRegs_[gp++], ebWidth(i), 8 * i, 0, false});
                    }
                }
                else
                    onStack();
            }
            out.push_back(std::move(slots));
        }
        stackCount = stk;
        return out;
    }

    // --- legalize -----------------------------------------------------------
    // x86 encoding fixup pass: rewrite instructions that violate x86 encoding
    // constraints. Operates on one function's instruction list in place.
    void legalize(std::vector<std::unique_ptr<Instruction>> &instrs) const override
    {
        std::vector<std::unique_ptr<Instruction>> rewritten;
        rewritten.reserve(instrs.size());

        for (auto &instr : instrs)
        {
            if (auto *m = dynamic_cast<MoveInstruction *>(instr.get()))
            {
                const int b = bytesOf(m->type);
                if (m->type == AssemblyType::DOUBLE && isMemory(*m->src) && isMemory(*m->dst))
                {
                    // movsd can't go memory->memory; stage through an XMM scratch.
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::XMM14, 8), AssemblyType::DOUBLE));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::XMM14, 8), std::move(m->dst), AssemblyType::DOUBLE));
                }
                // A 64-bit immediate or a memory source can't move straight to
                // memory; stage it in R10 first.
                else if (isMemory(*m->dst) && (isMemory(*m->src) || isLargeImmediate(*m->src)))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::R10, b), m->type));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::R10, b), std::move(m->dst), m->type));
                }
                else
                {
                    rewritten.push_back(std::move(instr));
                }
            }
            else if (auto *m = dynamic_cast<MoveSXInstruction *>(instr.get()))
            {
                // movsx needs a non-immediate source and a register destination.
                const AssemblyType st = m->srcType, dt = m->dstType;
                auto src = std::move(m->src);
                auto dst = std::move(m->dst);
                if (isImmediate(*src))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(src), reg(RegisterName::R10, bytesOf(st)), st));
                    src = reg(RegisterName::R10, bytesOf(st));
                }
                if (isMemory(*dst))
                {
                    rewritten.push_back(std::make_unique<MoveSXInstruction>(
                        std::move(src), reg(RegisterName::R11, bytesOf(dt)), st, dt));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::R11, bytesOf(dt)), std::move(dst), dt));
                }
                else
                {
                    rewritten.push_back(std::make_unique<MoveSXInstruction>(
                        std::move(src), std::move(dst), st, dt));
                }
            }
            else if (auto *m = dynamic_cast<MoveZeroExtendInstruction *>(instr.get()))
            {
                const AssemblyType st = m->srcType, dt = m->dstType;
                auto src = std::move(m->src);
                auto dst = std::move(m->dst);
                if (st == AssemblyType::BYTE)
                {
                    // movzbl/movzbq: source reg/mem (not immediate), register dest.
                    if (isImmediate(*src))
                    {
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            std::move(src), reg(RegisterName::R10, 1), AssemblyType::BYTE));
                        src = reg(RegisterName::R10, 1);
                    }
                    if (isMemory(*dst))
                    {
                        rewritten.push_back(std::make_unique<MoveZeroExtendInstruction>(
                            std::move(src), reg(RegisterName::R11, bytesOf(dt)), st, dt));
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            reg(RegisterName::R11, bytesOf(dt)), std::move(dst), dt));
                    }
                    else
                    {
                        rewritten.push_back(std::make_unique<MoveZeroExtendInstruction>(
                            std::move(src), std::move(dst), st, dt));
                    }
                }
                else
                {
                    // No movzlq exists: zero-extending 32->64 is a plain 32-bit mov,
                    // which clears the upper half of a register destination. A memory
                    // destination can't auto-zero its upper word, so route through R11.
                    if (isMemory(*dst))
                    {
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            std::move(src), reg(RegisterName::R11, 4), AssemblyType::LONGWORD));
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            reg(RegisterName::R11, 8), std::move(dst), AssemblyType::QUADWORD));
                    }
                    else
                    {
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            std::move(src), std::move(dst), AssemblyType::LONGWORD));
                    }
                }
            }
            else if (auto *m = dynamic_cast<IDivInstruction *>(instr.get());
                     m && isImmediate(*m->operand))
            {
                const int b = bytesOf(m->type);
                m->scratchRegisterInstruction = std::make_unique<MoveInstruction>(
                    std::move(m->operand), reg(RegisterName::R10, b), m->type);
                m->operand = reg(RegisterName::R10, b);
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<DivInstruction *>(instr.get());
                     m && isImmediate(*m->operand))
            {
                // div, like idiv, can't take an immediate divisor: stage it in R10.
                const int b = bytesOf(m->type);
                m->scratchRegisterInstruction = std::make_unique<MoveInstruction>(
                    std::move(m->operand), reg(RegisterName::R10, b), m->type);
                m->operand = reg(RegisterName::R10, b);
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<BinaryInstruction *>(instr.get()))
            {
                const int b = bytesOf(m->type);
                if (m->type == AssemblyType::DOUBLE)
                {
                    // SSE arithmetic (addsd/subsd/mulsd/divsd/xorpd) needs an XMM
                    // register destination: load it, operate, store back.
                    if (isMemory(*m->dst))
                    {
                        auto dstcopy1 = cloneMemory(m->dst.get());
                        auto dstcopy2 = cloneMemory(m->dst.get());
                        m->preStackFixInstruction = std::make_unique<MoveInstruction>(
                            std::move(dstcopy1), reg(RegisterName::XMM15, 8), AssemblyType::DOUBLE);
                        m->dst = reg(RegisterName::XMM15, 8);
                        m->postStackFixInstruction = std::make_unique<MoveInstruction>(
                            reg(RegisterName::XMM15, 8), std::move(dstcopy2), AssemblyType::DOUBLE);
                    }
                    rewritten.push_back(std::move(instr));
                    continue;
                }
                // A 64-bit immediate can't be an operand of any binary op, so stage
                // it in a register first (this also frees the fix slots for imul).
                if (isLargeImmediate(*m->src))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::R10, b), m->type));
                    m->src = reg(RegisterName::R10, b);
                }

                const bool addSubLogic = m->op == BinaryOp::Add || m->op == BinaryOp::Subtract ||
                                         m->op == BinaryOp::BitwiseAnd ||
                                         m->op == BinaryOp::BitwiseOr ||
                                         m->op == BinaryOp::BitwiseXor;
                if (addSubLogic && isMemory(*m->src) && isMemory(*m->dst))
                {
                    // add/sub/logic can't have both operands in memory.
                    m->preStackFixInstruction = std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::R10, b), m->type);
                    m->src = reg(RegisterName::R10, b);
                }
                else if (m->op == BinaryOp::Multiply && isMemory(*m->dst))
                {
                    // imul needs a register destination: load it, multiply, store back.
                    auto dstcopy1 = cloneMemory(m->dst.get());
                    auto dstcopy2 = cloneMemory(m->dst.get());
                    m->preStackFixInstruction = std::make_unique<MoveInstruction>(
                        std::move(dstcopy1), reg(RegisterName::R11, b), m->type);
                    m->dst = reg(RegisterName::R11, b);
                    m->postStackFixInstruction = std::make_unique<MoveInstruction>(
                        reg(RegisterName::R11, b), std::move(dstcopy2), m->type);
                }
                else if ((m->op == BinaryOp::LeftShift || m->op == BinaryOp::RightShift) &&
                         !isImmediate(*m->src))
                {
                    m->preStackFixInstruction = std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::CX, 4));
                    m->src = reg(RegisterName::CX, 1);
                }
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<CmpInstruction *>(instr.get()))
            {
                // The two operands may each need staging, so emit the moves directly
                // (one preStackFix slot can't hold both). `a` can't be a 64-bit
                // immediate or share memory with `b`; `b` can't be an immediate.
                const int b = bytesOf(m->type);
                if (m->type == AssemblyType::DOUBLE)
                {
                    // comisd/ucomisd: the second operand must be an XMM register.
                    if (isMemory(*m->b))
                    {
                        rewritten.push_back(std::make_unique<MoveInstruction>(
                            std::move(m->b), reg(RegisterName::XMM15, 8), AssemblyType::DOUBLE));
                        m->b = reg(RegisterName::XMM15, 8);
                    }
                    rewritten.push_back(std::move(instr));
                    continue;
                }
                if (isLargeImmediate(*m->a) || (isMemory(*m->a) && isMemory(*m->b)))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->a), reg(RegisterName::R10, b), m->type));
                    m->a = reg(RegisterName::R10, b);
                }
                if (isImmediate(*m->b))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->b), reg(RegisterName::R11, b), m->type));
                    m->b = reg(RegisterName::R11, b);
                }
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<CVTSI2SD *>(instr.get()))
            {
                // Source can be reg/mem but not an immediate; destination must be an
                // XMM register. (type is the integer source's width.)
                const int b = bytesOf(m->type);
                if (isImmediate(*m->src))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(m->src), reg(RegisterName::R10, b), m->type));
                    m->src = reg(RegisterName::R10, b);
                }
                if (isMemory(*m->dst))
                {
                    auto dstcopy = cloneMemory(m->dst.get());
                    m->dst = reg(RegisterName::XMM15, 8);
                    rewritten.push_back(std::move(instr));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::XMM15, 8), std::move(dstcopy), AssemblyType::DOUBLE));
                }
                else
                {
                    rewritten.push_back(std::move(instr));
                }
            }
            else if (auto *m = dynamic_cast<CVTTSD2SI *>(instr.get()))
            {
                // Destination must be a GP register. (type is the integer dest width.)
                const int b = bytesOf(m->type);
                if (isMemory(*m->dst))
                {
                    auto dstcopy = cloneMemory(m->dst.get());
                    m->dst = reg(RegisterName::R11, b);
                    rewritten.push_back(std::move(instr));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::R11, b), std::move(dstcopy), m->type));
                }
                else
                {
                    rewritten.push_back(std::move(instr));
                }
            }
            else if (auto *m = dynamic_cast<PushInstruction *>(instr.get());
                     m && isLargeImmediate(*m->a))
            {
                // pushq takes a register/memory/32-bit-immediate, not a 64-bit one.
                rewritten.push_back(std::make_unique<MoveInstruction>(
                    std::move(m->a), reg(RegisterName::R10, 8), AssemblyType::QUADWORD));
                rewritten.push_back(std::make_unique<PushInstruction>(reg(RegisterName::R10, 8)));
            }
            else if (auto *m = dynamic_cast<LeaInstruction *>(instr.get()))
            {
                auto src = std::move(m->src);
                auto dst = std::move(m->dst);
                if (isMemory(*dst))
                {
                    rewritten.push_back(std::make_unique<LeaInstruction>(
                        std::move(src), reg(RegisterName::R11, 8)));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::R11, 8), std::move(dst), AssemblyType::QUADWORD));
                }
                else
                {
                    rewritten.push_back(
                        std::make_unique<LeaInstruction>(std::move(src), std::move(dst)));
                }
            }
            else
            {
                rewritten.push_back(std::move(instr));
            }
        }

        instrs = std::move(rewritten);
    }

  private:
    // Register banks (returned by const-ref from intArgRegs/sseArgRegs).
    const std::vector<RegisterName> intArgRegs_{RegisterName::DI, RegisterName::SI,
                                                RegisterName::DX, RegisterName::CX,
                                                RegisterName::R8, RegisterName::R9};
    const std::vector<RegisterName> sseArgRegs_{
        RegisterName::XMM0, RegisterName::XMM1, RegisterName::XMM2, RegisterName::XMM3,
        RegisterName::XMM4, RegisterName::XMM5, RegisterName::XMM6, RegisterName::XMM7};

    // --- helpers used only by legalize() ------------------------------------

    static bool isImmediate(const Operand &op)
    {
        return dynamic_cast<const Immediate *>(&op) != nullptr;
    }

    static bool isData(const Operand &op) { return dynamic_cast<const Data *>(&op) != nullptr; }

    static bool isMemory(const Operand &op)
    {
        return dynamic_cast<const Memory *>(&op) != nullptr || isData(op);
    }

    static bool isLargeImmediate(const Operand &op)
    {
        auto *i = dynamic_cast<const Immediate *>(&op);
        return i && (i->value > 2147483647LL || i->value < -2147483648LL);
    }

    static std::unique_ptr<Register> reg(RegisterName n, int bytes)
    {
        return std::make_unique<Register>(n, bytes);
    }

    static std::unique_ptr<Operand> cloneMemory(const Operand *op)
    {
        if (auto *d = dynamic_cast<const Data *>(op))
            return std::make_unique<Data>(*d);
        if (auto *m = dynamic_cast<const Memory *>(op))
            return std::make_unique<Memory>(*m);
        return nullptr;
    }

    // Assembly width of an AssemblyType in bytes.
    static int bytesOf(AssemblyType t)
    {
        if (t == AssemblyType::BYTE)
            return 1;
        return (t == AssemblyType::QUADWORD || t == AssemblyType::DOUBLE) ? 8 : 4;
    }
};
