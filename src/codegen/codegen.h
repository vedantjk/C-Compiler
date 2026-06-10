#pragma once

#include "../registerallocator/registerAllocator.h"
#include "../support/RTTI.h"
#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "ABI.h"
#include "ast/TopLevelNodes/codegenStaticConstant.h"
#include "ast/TopLevelNodes/codegenStaticVariable.h"
#include "instructions/instructions.h"
#include "tacky/ast/TopLevelNodes/TackyStaticVariable.h"

#include <algorithm>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
class codegenDriver
{
    const ABI &abi_;

    // Per-name object facts recovered from self-typed operands at the start of the
    // stack-slot pass: whether the name is static (RIP-relative) and, for an
    // aggregate, the data needed to size and align its slot. Built fresh per function.
    struct ObjInfo
    {
        bool isStatic = false;
        long long objSize = 0; // array byte size; 0 unless an array
        int objAlign = 0;      // array alignment
        std::string structTag; // non-empty => struct (classified on demand)
    };

    int constCounter = 0;
    std::map<std::pair<uint64_t, int>, std::string> constLabels;
    std::vector<std::unique_ptr<codegenStaticConstant>> constPool;

    // Unique local labels for branch targets synthesized in codegen (e.g. the
    // out-of-range path in unsigned->double conversion).
    int labelCounter = 0;
    std::string uniqueLabel(const std::string &prefix)
    {
        return prefix + std::to_string(labelCounter++);
    }

    std::string internDouble(double value, int alignment)
    {
        const auto bits = std::bit_cast<uint64_t>(value);
        auto [it, inserted] = constLabels.try_emplace({bits, alignment}, "");
        if (inserted)
        {
            it->second = ".Lconst" + std::to_string(constCounter++);
            constPool.push_back(
                std::make_unique<codegenStaticConstant>(0, 0, it->second, alignment, value));
        }
        return it->second;
    }

  public:
    explicit codegenDriver(const ABI &abi) : abi_(abi) {}

    static bool isImmediate(const Operand &op)
    {
        return dynamic_cast<const Immediate *>(&op) != nullptr;
    }

    // Assembly width of a TACKY value: char -> 1-byte, long/pointer -> 8-byte
    // quadword, double -> SSE, else 4-byte longword.
    static AssemblyType toAssemblyType(const ConstantType t)
    {
        if (t == ConstantType::DOUBLE)
            return AssemblyType::DOUBLE;
        if (ctBytes(t) == 1)
            return AssemblyType::BYTE;
        return ctBytes(t) == 8 ? AssemblyType::QUADWORD : AssemblyType::LONGWORD;
    }
    static AssemblyType assemblyTypeOf(const TackyVal &v) { return toAssemblyType(typeOf(v)); }
    // Register width that matches an assembly type. Quadword and double are both
    // 8 bytes; longword is 4; byte is 1.
    static int bytesOf(const AssemblyType t)
    {
        if (t == AssemblyType::BYTE)
            return 1;
        return (t == AssemblyType::QUADWORD || t == AssemblyType::DOUBLE) ? 8 : 4;
    }

    std::unique_ptr<Operand> tackyValToOperand(const TackyVal &t)
    {
        if (auto *c = std::get_if<TackyConstant>(&t))
        {
            return std::make_unique<Immediate>(c->value);
        }
        if (auto *f = std::get_if<TackyFloatingConstant>(&t))
        {
            return std::make_unique<Data>(internDouble(f->value, 8));
        }
        if (auto *v = std::get_if<TackyVar>(&t))
        {
            auto pr = std::make_unique<PseudoRegister>(v->name, assemblyTypeOf(t));
            // Copy self-typing fields from the TackyVar. Inert this step (CG2 reads
            // them in lowerPseudoSlot); side-maps remain authoritative.
            pr->isStatic = v->isStatic;
            pr->objSize = v->objSize;
            pr->objAlign = v->objAlign;
            pr->structTag = v->structTag;
            return pr;
        }
        return nullptr;
    }

    void processTackyUnary(const TackyUnary &tackyUnary,
                           std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyUnary.src);
        auto dst1 = tackyValToOperand(tackyUnary.dst);
        auto dst2 = tackyValToOperand(tackyUnary.dst);
        const AssemblyType srcType = assemblyTypeOf(tackyUnary.src);
        const AssemblyType dstType = assemblyTypeOf(tackyUnary.dst);

        if (tackyUnary.op == UnaryOp::Negate && isDouble(typeOf(tackyUnary.dst)))
        {
            auto mask = std::make_unique<Data>(internDouble(-0.0, 16));
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::move(src), std::move(dst1), AssemblyType::DOUBLE));
            instructions.push_back(std::make_unique<BinaryInstruction>(
                std::move(mask), std::move(dst2), BinaryOp::Xor, AssemblyType::DOUBLE));
            return;
        }

        if (tackyUnary.op == UnaryOp::Not)
        {
            if (isDouble(typeOf(tackyUnary.src)))
            {
                // xorpd %xmm15, %xmm15   -> zero the scratch
                // 2. ucomisd src, %xmm15    -> compare src against 0.0
                // 3. mov $0, dst            (int result)
                // 4. sete dst
                instructions.push_back(std::make_unique<BinaryInstruction>(
                    std::make_unique<Register>(RegisterName::XMM15, 8),
                    std::make_unique<Register>(RegisterName::XMM15, 8), BinaryOp::Xor,
                    AssemblyType::DOUBLE));

                instructions.push_back(std::make_unique<CmpInstruction>(
                    std::move(src), std::make_unique<Register>(RegisterName::XMM15, 8),
                    AssemblyType::DOUBLE));

                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::make_unique<Immediate>(0), std::move(dst1), dstType));

                instructions.push_back(
                    std::make_unique<SetCCInstruction>(CondCode::E, std::move(dst2)));
                return;
            }
            std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
            std::unique_ptr<Operand> imm2 = std::make_unique<Immediate>(0);
            instructions.push_back(
                std::make_unique<CmpInstruction>(std::move(imm1), std::move(src), srcType));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(imm2), std::move(dst1), dstType));
            instructions.push_back(
                std::make_unique<SetCCInstruction>(CondCode::E, std::move(dst2)));
            return;
        }

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src), std::move(dst1), dstType));
        instructions.push_back(
            std::make_unique<UnaryInstruction>(std::move(dst2), tackyUnary.op, dstType));
    }

    void processTackyReturn(const TackyReturn &tackyReturn,
                            std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        const TackyVar *structVal =
            tackyReturn.val ? std::get_if<TackyVar>(&*tackyReturn.val) : nullptr;
        if (structVal && !structVal->structTag.empty())
        {
            const StructABI abi = classifyStruct(structVal->structTag);
            const std::string &name = structVal->name;
            const std::string &tag = structVal->structTag;
            const bool tagStatic = structVal->isStatic;
            std::vector<RegisterName> retRegs;
            if (abi.inMemory)
            {
                // Copy the struct into the caller's space (the saved hidden pointer),
                // then return that pointer in RAX. The copy uses 8/4/1-byte chunks so
                // it never writes past the caller-provided object.
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::make_unique<PseudoRegister>(curRetPtrName, AssemblyType::QUADWORD),
                    std::make_unique<Register>(RegisterName::AX, 8), AssemblyType::QUADWORD));
                long long off = 0, size = abi.size;
                auto chunk = [&](long long w, AssemblyType t)
                {
                    while (size - off >= w)
                    {
                        instructions.push_back(std::make_unique<MoveInstruction>(
                            aggMem(name, static_cast<int>(off), t, tag, tagStatic),
                            std::make_unique<Register>(RegisterName::R10, bytesOf(t)), t));
                        instructions.push_back(std::make_unique<MoveInstruction>(
                            std::make_unique<Register>(RegisterName::R10, bytesOf(t)),
                            std::make_unique<Memory>(RegisterName::AX, static_cast<int>(off)), t));
                        off += w;
                    }
                };
                chunk(8, AssemblyType::QUADWORD);
                chunk(4, AssemblyType::LONGWORD);
                chunk(1, AssemblyType::BYTE);
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::make_unique<PseudoRegister>(curRetPtrName, AssemblyType::QUADWORD),
                    std::make_unique<Register>(RegisterName::AX, 8), AssemblyType::QUADWORD));
                retRegs.push_back(RegisterName::AX);
            }
            else
            {
                int gpi = 0, ssei = 0;
                const RegisterName gpRegs[2] = {RegisterName::AX, RegisterName::DX};
                const RegisterName sseRegs[2] = {RegisterName::XMM0, RegisterName::XMM1};
                for (int i = 0; i < static_cast<int>(abi.classes.size()); i++)
                {
                    const int bc = static_cast<int>(std::min<long long>(8, abi.size - 8 * i));
                    if (abi.classes[i] == Eightbyte::SSE)
                    {
                        retRegs.push_back(sseRegs[ssei]);
                        emitLoadEightbyte(name, 8 * i, 8, true, sseRegs[ssei++], instructions, tag,
                                          tagStatic);
                    }
                    else
                    {
                        retRegs.push_back(gpRegs[gpi]);
                        emitLoadEightbyte(name, 8 * i, bc, false, gpRegs[gpi++], instructions, tag,
                                          tagStatic);
                    }
                }
            }
            instructions.push_back(std::make_unique<ReturnInstruction>(std::move(retRegs)));
            return;
        }

        std::vector<RegisterName> retRegs;
        if (tackyReturn.val)
        {
            const AssemblyType t = assemblyTypeOf(*tackyReturn.val);
            std::unique_ptr<Operand> source = tackyValToOperand(*tackyReturn.val);
            const bool dbl = t == AssemblyType::DOUBLE;
            retRegs.push_back(dbl ? RegisterName::XMM0 : RegisterName::AX);
            auto dest =
                std::make_unique<Register>(dbl ? RegisterName::XMM0 : RegisterName::AX, bytesOf(t));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(source), std::move(dest), t));
        }
        instructions.push_back(std::make_unique<ReturnInstruction>(std::move(retRegs)));
    }

    void processDivision(const TackyBinary &tackyBinary,
                         std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        const AssemblyType t = assemblyTypeOf(tackyBinary.dst);
        const bool uns = isUnsignedCt(typeOf(tackyBinary.src1));
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAXa = std::make_unique<Register>(RegisterName::AX, bytesOf(t));
        std::unique_ptr<Operand> regAXb = std::make_unique<Register>(RegisterName::AX, bytesOf(t));
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src1), std::move(regAXa), t));
        if (uns)
        {
            // Unsigned: zero the high word (mov $0, %edx/%rdx) instead of sign-
            // extending with cdq, then use div rather than idiv.
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Immediate>(0),
                std::make_unique<Register>(RegisterName::DX, bytesOf(t)), t));
            instructions.push_back(std::make_unique<DivInstruction>(std::move(src2), t));
        }
        else
        {
            instructions.push_back(std::make_unique<CdqInstruction>(t));
            instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2), t));
        }
        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(regAXb), std::move(dst), t));
    }

    void processRemainder(const TackyBinary &tackyBinary,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        const AssemblyType t = assemblyTypeOf(tackyBinary.dst);
        const bool uns = isUnsignedCt(typeOf(tackyBinary.src1));
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAX = std::make_unique<Register>(RegisterName::AX, bytesOf(t));
        std::unique_ptr<Operand> regDX = std::make_unique<Register>(RegisterName::DX, bytesOf(t));
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src1), std::move(regAX), t));
        if (uns)
        {
            // Unsigned: zero %edx/%rdx then div; the remainder still lands in DX.
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Immediate>(0),
                std::make_unique<Register>(RegisterName::DX, bytesOf(t)), t));
            instructions.push_back(std::make_unique<DivInstruction>(std::move(src2), t));
        }
        else
        {
            instructions.push_back(std::make_unique<CdqInstruction>(t));
            instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2), t));
        }
        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(regDX), std::move(dst), t));
    }

    static bool isRelationalOp(const BinaryOp op)
    {
        return op == BinaryOp::Equal || op == BinaryOp::NotEqual || op == BinaryOp::LessThan ||
               op == BinaryOp::LessOrEqual || op == BinaryOp::GreaterThan ||
               op == BinaryOp::GreaterThanOrEqual;
    }

    static CondCode binaryOpToCondCode(const BinaryOp op, const bool uns)
    {
        // Equality is signedness-agnostic; the ordered comparisons pick the
        // unsigned codes (above/below) vs. the signed ones (greater/less).
        if (op == BinaryOp::Equal)
            return CondCode::E;
        if (op == BinaryOp::NotEqual)
            return CondCode::NE;
        if (op == BinaryOp::LessThan)
            return uns ? CondCode::B : CondCode::L;
        if (op == BinaryOp::LessOrEqual)
            return uns ? CondCode::BE : CondCode::LE;
        if (op == BinaryOp::GreaterThan)
            return uns ? CondCode::A : CondCode::G;
        if (op == BinaryOp::GreaterThanOrEqual)
            return uns ? CondCode::AE : CondCode::GE;

        throw std::runtime_error("Illegal operation forwarded to convert to cond code");
    }

    void processTackyBinary(const TackyBinary &tackyBinary,
                            std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        if (tackyBinary.op == BinaryOp::Divide && !isDouble(typeOf(tackyBinary.dst)))
        {
            processDivision(tackyBinary, instructions);
            return;
        }
        if (tackyBinary.op == BinaryOp::Remainder)
        {
            processRemainder(tackyBinary, instructions);
            return;
        }
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        auto dst1 = tackyValToOperand(tackyBinary.dst);
        auto dst2 = tackyValToOperand(tackyBinary.dst);
        const AssemblyType dstType = assemblyTypeOf(tackyBinary.dst);

        if (isRelationalOp(tackyBinary.op))
        {
            const bool useUnsignedCC =
                isUnsignedCt(typeOf(tackyBinary.src1)) || isDouble(typeOf(tackyBinary.src1));
            // CondCode for floating pt and unsigned is the same.
            CondCode cc = binaryOpToCondCode(tackyBinary.op, useUnsignedCC);
            const AssemblyType cmpType = assemblyTypeOf(tackyBinary.src1);
            std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
            instructions.push_back(
                std::make_unique<CmpInstruction>(std::move(src2), std::move(src1), cmpType));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(imm1), std::move(dst1), dstType));
            instructions.push_back(std::make_unique<SetCCInstruction>(cc, std::move(dst2)));
            return;
        }

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src1), std::move(dst1), dstType));
        instructions.push_back(std::make_unique<BinaryInstruction>(std::move(src2), std::move(dst2),
                                                                   tackyBinary.op, dstType));
    }

    void processTackyJump(const TackyJump &tackyJump,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        instructions.push_back(std::make_unique<JumpInstruction>(tackyJump.identifier));
    }

    void processTackyJumpIfZero(const TackyJumpIfZero &tackyJIZ,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto condition = tackyValToOperand(tackyJIZ.condition);
        if (isDouble(typeOf(tackyJIZ.condition)))
        {
            // No immediate compare for doubles: zero a scratch XMM with xorpd and
            // compare the condition against it (ucomisd cond, %xmm15).
            instructions.push_back(std::make_unique<BinaryInstruction>(
                std::make_unique<Register>(RegisterName::XMM15, 8),
                std::make_unique<Register>(RegisterName::XMM15, 8), BinaryOp::Xor,
                AssemblyType::DOUBLE));
            instructions.push_back(std::make_unique<CmpInstruction>(
                std::move(condition), std::make_unique<Register>(RegisterName::XMM15, 8),
                AssemblyType::DOUBLE));
            instructions.push_back(
                std::make_unique<JumpCCInstruction>(CondCode::E, tackyJIZ.identifier));
            return;
        }
        std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
        instructions.push_back(std::make_unique<CmpInstruction>(
            std::move(imm1), std::move(condition), assemblyTypeOf(tackyJIZ.condition)));
        instructions.push_back(
            std::make_unique<JumpCCInstruction>(CondCode::E, tackyJIZ.identifier));
    }

    void processTackyJumpIfNotZero(const TackyJumpIfNotZero &tackyJIZ,
                                   std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto condition = tackyValToOperand(tackyJIZ.condition);
        if (isDouble(typeOf(tackyJIZ.condition)))
        {
            // No immediate compare for doubles: zero a scratch XMM with xorpd and
            // compare the condition against it (ucomisd cond, %xmm15).
            instructions.push_back(std::make_unique<BinaryInstruction>(
                std::make_unique<Register>(RegisterName::XMM15, 8),
                std::make_unique<Register>(RegisterName::XMM15, 8), BinaryOp::Xor,
                AssemblyType::DOUBLE));
            instructions.push_back(std::make_unique<CmpInstruction>(
                std::move(condition), std::make_unique<Register>(RegisterName::XMM15, 8),
                AssemblyType::DOUBLE));
            instructions.push_back(
                std::make_unique<JumpCCInstruction>(CondCode::NE, tackyJIZ.identifier));
            return;
        }
        std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
        instructions.push_back(std::make_unique<CmpInstruction>(
            std::move(imm1), std::move(condition), assemblyTypeOf(tackyJIZ.condition)));
        instructions.push_back(
            std::make_unique<JumpCCInstruction>(CondCode::NE, tackyJIZ.identifier));
    }

    void processTackyCopy(const TackyCopy &tackyCopy,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyCopy.src);
        auto dst = tackyValToOperand(tackyCopy.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src), std::move(dst),
                                                                 assemblyTypeOf(tackyCopy.dst)));
    }

    void processTackyLabel(const TackyLabel &tackyLabel,
                           std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        instructions.push_back(std::make_unique<Label>(tackyLabel.identifier));
    }

    // --- System V struct argument/return classification ------------------- //

    // Per-function return state, set in processFunction and read by TackyReturn.
    bool curReturnsStruct = false;
    StructABI curReturnABI;
    std::string curRetPtrName; // slot holding the hidden destination pointer

    // Slot and Arg are defined in ABI.h (ABI vocabulary shared with the driver).

    Arg makeArg(const TackyVal &v)
    {
        Arg a;
        if (auto *var = std::get_if<TackyVar>(&v); var && !var->structTag.empty())
        {
            a.isStruct = true;
            a.isStatic = var->isStatic;
            a.name = var->name;
            a.structTag = var->structTag;
            a.abi = classifyStruct(var->structTag);
            return a;
        }
        a.val = v;
        a.stype = assemblyTypeOf(v);
        a.sdouble = typeOf(v) == ConstantType::DOUBLE;
        return a;
    }

    // A PseudoMem into a named aggregate, carrying the object's static-ness and struct
    // tag so the stack-slot pass can recover both placement (RIP data vs stack slot)
    // and slot size from any one reference to it.
    static std::unique_ptr<PseudoMem> aggMem(const std::string &name, int off, AssemblyType t,
                                             const std::string &structTag, bool isStatic)
    {
        auto m = std::make_unique<PseudoMem>(name, off, t);
        m->structTag = structTag;
        m->isStatic = isStatic;
        return m;
    }

    // The memory operand for a slot's struct eightbyte (scalars use tackyValToOperand).
    std::unique_ptr<Operand> slotMem(const Arg &a, const Slot &s)
    {
        return aggMem(a.name, s.objOff,
                      s.where == Slot::SSE_REG ? AssemblyType::DOUBLE : AssemblyType::QUADWORD,
                      a.structTag, a.isStatic);
    }

    // Load one eightbyte of struct object `name` (at byte `off`) into register
    // `reg`. A full 8-byte eightbyte (or any SSE eightbyte) is a single mov; a
    // partial tail (<8 bytes) is assembled byte by byte so we never read past the
    // object — required when it sits at the end of a mapped page.
    void emitLoadEightbyte(const std::string &name, int off, int byteCount, bool sse,
                           RegisterName reg,
                           std::vector<std::unique_ptr<Instruction>> &instructions,
                           const std::string &structTag, bool isStatic)
    {
        if (sse || byteCount >= 8)
        {
            const AssemblyType t = sse ? AssemblyType::DOUBLE : AssemblyType::QUADWORD;
            instructions.push_back(std::make_unique<MoveInstruction>(
                aggMem(name, off, t, structTag, isStatic), std::make_unique<Register>(reg, 8), t));
            return;
        }
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Immediate>(0), std::make_unique<Register>(reg, 4),
            AssemblyType::LONGWORD)); // zero the whole register
        for (int k = byteCount - 1; k >= 0; --k)
        {
            instructions.push_back(std::make_unique<BinaryInstruction>(
                std::make_unique<Immediate>(8), std::make_unique<Register>(reg, 8),
                BinaryOp::LeftShift, AssemblyType::QUADWORD));
            instructions.push_back(std::make_unique<MoveInstruction>(
                aggMem(name, off + k, AssemblyType::BYTE, structTag, isStatic),
                std::make_unique<Register>(reg, 1), AssemblyType::BYTE));
        }
    }

    void processTackyFunctionCall(const TackyFunctionCall &tackyFunctionCall,
                                  std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        // Is this a struct return, and if so does it travel via a hidden pointer?
        const auto *dstVar = std::get_if<TackyVar>(&tackyFunctionCall.dst);
        const bool structRet = dstVar && !dstVar->structTag.empty();
        const StructABI dstABI = structRet ? classifyStruct(dstVar->structTag) : StructABI{};
        const bool returnsMemory = structRet && dstABI.inMemory;
        const std::string dstTag = structRet ? dstVar->structTag : std::string{};
        const bool dstStatic = structRet && dstVar->isStatic;

        std::vector<Arg> args;
        args.reserve(tackyFunctionCall.args.size());
        for (const auto &a : tackyFunctionCall.args)
            args.push_back(makeArg(a));

        int stackCount = 0;
        auto placement = abi_.computeSlots(args, returnsMemory, stackCount);

        int xmmUsed = 0;
        for (const auto &slots : placement)
            for (const auto &s : slots)
                if (s.where == Slot::SSE_REG)
                    xmmUsed++;

        const int stackPadding = (stackCount % 2) ? 8 : 0;
        if (stackPadding)
            instructions.push_back(
                std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(stackPadding),
                                                    std::make_unique<Register>(RegisterName::SP, 8),
                                                    BinaryOp::Subtract, AssemblyType::QUADWORD));

        // Push stack arguments in reverse object order (so the first ends up lowest).
        std::vector<std::pair<const Arg *, Slot>> stackSlots;
        for (size_t i = 0; i < args.size(); i++)
            for (const auto &s : placement[i])
                if (s.where == Slot::STACK)
                    stackSlots.push_back({&args[i], s});
        for (auto it = stackSlots.rbegin(); it != stackSlots.rend(); ++it)
        {
            const Arg &a = *it->first;
            const Slot &s = it->second;
            if (!s.scalar)
            {
                // A full eightbyte can be pushed straight from memory; a partial tail
                // is assembled into RAX first so we never read past the object.
                if (s.width >= 8)
                    instructions.push_back(std::make_unique<PushInstruction>(slotMem(a, s)));
                else
                {
                    emitLoadEightbyte(a.name, s.objOff, s.width, false, RegisterName::AX,
                                      instructions, a.structTag, a.isStatic);
                    instructions.push_back(std::make_unique<PushInstruction>(
                        std::make_unique<Register>(RegisterName::AX, 8)));
                }
                continue;
            }
            auto op = tackyValToOperand(s.val);
            if (isImmediate(*op) || s.width == 8)
                instructions.push_back(std::make_unique<PushInstruction>(std::move(op)));
            else
            {
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::move(op), std::make_unique<Register>(RegisterName::AX, s.width),
                    assemblyTypeOf(s.val)));
                instructions.push_back(std::make_unique<PushInstruction>(
                    std::make_unique<Register>(RegisterName::AX, 8)));
            }
        }

        std::vector<RegisterName> argRegs;
        // Load register arguments.
        for (size_t i = 0; i < args.size(); i++)
        {
            const Arg &a = args[i];
            for (const auto &s : placement[i])
            {
                if (s.where == Slot::STACK)
                    continue;
                argRegs.push_back(s.reg);
                if (s.where == Slot::SSE_REG)
                {
                    auto src = s.scalar ? tackyValToOperand(s.val) : slotMem(a, s);
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        std::move(src), std::make_unique<Register>(s.reg, 8),
                        AssemblyType::DOUBLE));
                }
                else if (s.scalar)
                {
                    const AssemblyType t = assemblyTypeOf(s.val);
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        tackyValToOperand(s.val), std::make_unique<Register>(s.reg, bytesOf(t)),
                        t));
                }
                else
                {
                    // Struct eightbyte: a full eightbyte is one mov, a partial tail is
                    // assembled byte by byte (no read past the object).
                    emitLoadEightbyte(a.name, s.objOff, s.width, false, s.reg, instructions,
                                      a.structTag, a.isStatic);
                }
            }
        }

        // Hidden return pointer for a MEMORY-class struct return goes in RDI.
        if (returnsMemory)
        {
            instructions.push_back(std::make_unique<LeaInstruction>(
                aggMem(dstVar->name, 0, AssemblyType::QUADWORD, dstTag, dstStatic),
                std::make_unique<Register>(RegisterName::DI, 8)));
            argRegs.push_back(RegisterName::DI);
        }

        if (tackyFunctionCall.variadic)
        {
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Immediate>(xmmUsed),
                std::make_unique<Register>(RegisterName::AX, 4), AssemblyType::LONGWORD));
            argRegs.push_back(RegisterName::AX);
        }

        instructions.push_back(
            std::make_unique<CallInstruction>(tackyFunctionCall.funcName, std::move(argRegs)));

        if (int bytesToRemove = 8 * stackCount + stackPadding)
            instructions.push_back(
                std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(bytesToRemove),
                                                    std::make_unique<Register>(RegisterName::SP, 8),
                                                    BinaryOp::Add, AssemblyType::QUADWORD));

        // Retrieve the return value.
        if (structRet)
        {
            // A MEMORY return was written through the hidden pointer (into dst);
            // a register return is unpacked from RAX/RDX (INTEGER) and XMM0/XMM1 (SSE).
            if (!returnsMemory)
            {
                int gpi = 0, ssei = 0;
                const RegisterName gpRegs[2] = {RegisterName::AX, RegisterName::DX};
                const RegisterName sseRegs[2] = {RegisterName::XMM0, RegisterName::XMM1};
                for (int i = 0; i < static_cast<int>(dstABI.classes.size()); i++)
                {
                    if (dstABI.classes[i] == Eightbyte::SSE)
                        instructions.push_back(std::make_unique<MoveInstruction>(
                            std::make_unique<Register>(sseRegs[ssei++], 8),
                            aggMem(dstVar->name, 8 * i, AssemblyType::DOUBLE, dstTag, dstStatic),
                            AssemblyType::DOUBLE));
                    else
                        instructions.push_back(std::make_unique<MoveInstruction>(
                            std::make_unique<Register>(gpRegs[gpi++], 8),
                            aggMem(dstVar->name, 8 * i, AssemblyType::QUADWORD, dstTag, dstStatic),
                            AssemblyType::QUADWORD));
                }
            }
            return;
        }

        const AssemblyType retType = assemblyTypeOf(tackyFunctionCall.dst);
        auto assemblyDst = tackyValToOperand(tackyFunctionCall.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Register>(retType == AssemblyType::DOUBLE ? RegisterName::XMM0
                                                                       : RegisterName::AX,
                                       bytesOf(retType)),
            std::move(assemblyDst), retType));
    }

    void processTackySignExtend(const TackySignExtend &tackySignExtend,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackySignExtend.src);
        auto dst = tackyValToOperand(tackySignExtend.dst);
        instructions.push_back(std::make_unique<MoveSXInstruction>(
            std::move(src), std::move(dst), assemblyTypeOf(tackySignExtend.src),
            assemblyTypeOf(tackySignExtend.dst)));
    }

    void processTackyZeroExtend(const TackyZeroExtend &tackyZeroExtend,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyZeroExtend.src);
        auto dst = tackyValToOperand(tackyZeroExtend.dst);
        instructions.push_back(std::make_unique<MoveZeroExtendInstruction>(
            std::move(src), std::move(dst), assemblyTypeOf(tackyZeroExtend.src),
            assemblyTypeOf(tackyZeroExtend.dst)));
    }

    void processTackyTruncate(const TackyTruncate &tackyTruncate,
                              std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        // Truncation keeps the low bytes of the source: a plain move at the
        // destination's width (4 bytes for long->int, 1 byte for int->char).
        auto src = tackyValToOperand(tackyTruncate.src);
        auto dst = tackyValToOperand(tackyTruncate.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::move(src), std::move(dst), assemblyTypeOf(tackyTruncate.dst)));
    }

    void processTackyIntToDouble(const TackyIntToDouble &tackyIntToDouble,
                                 std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyIntToDouble.src);
        auto dst = tackyValToOperand(tackyIntToDouble.dst);
        instructions.push_back(std::make_unique<CVTSI2SD>(std::move(src), std::move(dst),
                                                          assemblyTypeOf(tackyIntToDouble.src)));
    }

    void processTackyDoubleToInt(const TackyDoubleToInt &tackyDoubleToInt,
                                 std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyDoubleToInt.src);
        auto dst = tackyValToOperand(tackyDoubleToInt.dst);
        instructions.push_back(std::make_unique<CVTTSD2SI>(std::move(src), std::move(dst),
                                                           assemblyTypeOf(tackyDoubleToInt.dst)));
    }

    void processTackyUIntToDouble(const TackyUIntToDouble &tackyUIntToDouble,
                                  std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyUIntToDouble.src);
        auto dst = tackyValToOperand(tackyUIntToDouble.dst);
        if (typeOf(tackyUIntToDouble.src) == ConstantType::UINT)
        {
            std::unique_ptr<Operand> reg_a = std::make_unique<Register>(RegisterName::AX, 4);
            std::unique_ptr<Operand> reg_b = std::make_unique<Register>(RegisterName::AX, 8);
            instructions.push_back(
                std::make_unique<MoveZeroExtendInstruction>(std::move(src), std::move(reg_a)));
            instructions.push_back(std::make_unique<CVTSI2SD>(std::move(reg_b), std::move(dst),
                                                              AssemblyType::QUADWORD));
            return;
        }

        // Unsigned long -> double. cvtsi2sd reads its source as signed, so a value
        // with the top bit set (>= 2^63) would be misread as negative. Branch on
        // the sign bit:
        //   cmp $0, src ; jl large        (signed-negative <=> top bit set)
        //   in range  -> cvtsi2sdq src, dst ; jmp end
        //   out of range (large):
        //     mov src, %rax ; mov src, %rdx
        //     shr %rax                    (rax = src >> 1)
        //     and $1, %rdx                (rdx = src & 1, the dropped low bit)
        //     or  %rdx, %rax              (round to odd: keep the dropped bit)
        //     cvtsi2sdq %rax, dst         (now in signed range)
        //     addsd dst, dst              (double it back)
        // Rounding to odd before halving stops the convert from double-rounding.
        const std::string largeLabel = uniqueLabel("u2d_large");
        const std::string endLabel = uniqueLabel("u2d_end");

        instructions.push_back(std::make_unique<CmpInstruction>(
            std::make_unique<Immediate>(0), std::move(src), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<JumpCCInstruction>(CondCode::L, largeLabel));

        instructions.push_back(std::make_unique<CVTSI2SD>(tackyValToOperand(tackyUIntToDouble.src),
                                                          std::move(dst), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<JumpInstruction>(endLabel));

        instructions.push_back(std::make_unique<Label>(largeLabel));
        instructions.push_back(std::make_unique<MoveInstruction>(
            tackyValToOperand(tackyUIntToDouble.src),
            std::make_unique<Register>(RegisterName::AX, 8), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<MoveInstruction>(
            tackyValToOperand(tackyUIntToDouble.src),
            std::make_unique<Register>(RegisterName::DX, 8), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<UnaryInstruction>(
            std::make_unique<Register>(RegisterName::AX, 8), UnaryOp::Shr, AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<BinaryInstruction>(
            std::make_unique<Immediate>(1), std::make_unique<Register>(RegisterName::DX, 8),
            BinaryOp::BitwiseAnd, AssemblyType::QUADWORD));
        instructions.push_back(
            std::make_unique<BinaryInstruction>(std::make_unique<Register>(RegisterName::DX, 8),
                                                std::make_unique<Register>(RegisterName::AX, 8),
                                                BinaryOp::BitwiseOr, AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<CVTSI2SD>(
            std::make_unique<Register>(RegisterName::AX, 8),
            tackyValToOperand(tackyUIntToDouble.dst), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<BinaryInstruction>(
            tackyValToOperand(tackyUIntToDouble.dst), tackyValToOperand(tackyUIntToDouble.dst),
            BinaryOp::Add, AssemblyType::DOUBLE));
        instructions.push_back(std::make_unique<Label>(endLabel));
    }

    void processTackyDoubleToUInt(const TackyDoubleToUInt &tackyDoubleToUInt,
                                  std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyDoubleToUInt.src);
        auto dst = tackyValToOperand(tackyDoubleToUInt.dst);
        if (typeOf(tackyDoubleToUInt.dst) == ConstantType::UINT)
        {
            // double -> unsigned int: cvttsd2si is signed, but a value < 2^32 always
            // fits in a signed quadword, so convert to a quadword and keep the low 32.
            instructions.push_back(std::make_unique<CVTTSD2SI>(
                std::move(src), std::make_unique<Register>(RegisterName::AX, 8),
                AssemblyType::QUADWORD));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::make_unique<Register>(RegisterName::AX, 4),
                                                  std::move(dst), AssemblyType::LONGWORD));
            return;
        }

        // double -> unsigned long. cvttsd2si yields a SIGNED result, so a value >= 2^63
        // would overflow it. Branch on whether src >= 2^63:
        //   comisd 2^63, src ; jae large
        //   in range -> cvttsd2siq src, dst ; jmp end
        //   large (src >= 2^63):
        //     movsd src, %xmm15
        //     subsd 2^63, %xmm15        (bring into signed range)
        //     cvttsd2siq %xmm15, dst
        //     movabsq $2^63, %rax ; addq %rax, dst   (add the bias back)
        // 2^63 is exactly representable as a double and is the signed-range boundary.
        const std::string bound = internDouble(9223372036854775808.0, 8);
        const std::string largeLabel = uniqueLabel("d2u_large");
        const std::string endLabel = uniqueLabel("d2u_end");

        instructions.push_back(std::make_unique<CmpInstruction>(
            std::make_unique<Data>(bound), std::move(src), AssemblyType::DOUBLE));
        instructions.push_back(std::make_unique<JumpCCInstruction>(CondCode::AE, largeLabel));

        instructions.push_back(std::make_unique<CVTTSD2SI>(tackyValToOperand(tackyDoubleToUInt.src),
                                                           std::move(dst), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<JumpInstruction>(endLabel));

        instructions.push_back(std::make_unique<Label>(largeLabel));
        instructions.push_back(std::make_unique<MoveInstruction>(
            tackyValToOperand(tackyDoubleToUInt.src),
            std::make_unique<Register>(RegisterName::XMM15, 8), AssemblyType::DOUBLE));
        instructions.push_back(std::make_unique<BinaryInstruction>(
            std::make_unique<Data>(bound), std::make_unique<Register>(RegisterName::XMM15, 8),
            BinaryOp::Subtract, AssemblyType::DOUBLE));
        instructions.push_back(std::make_unique<CVTTSD2SI>(
            std::make_unique<Register>(RegisterName::XMM15, 8),
            tackyValToOperand(tackyDoubleToUInt.dst), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Immediate>(static_cast<long long>(0x8000000000000000ULL)),
            std::make_unique<Register>(RegisterName::AX, 8), AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<BinaryInstruction>(
            std::make_unique<Register>(RegisterName::AX, 8),
            tackyValToOperand(tackyDoubleToUInt.dst), BinaryOp::Add, AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<Label>(endLabel));
    }

    void processTackyLoad(const TackyLoad &tackyLoad,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto ptr = tackyValToOperand(tackyLoad.srcPtr);
        auto dst = tackyValToOperand(tackyLoad.dst);
        auto dstType = assemblyTypeOf(tackyLoad.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::move(ptr), std::make_unique<Register>(RegisterName::AX, 8),
            AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Memory>(RegisterName::AX, 0), std::move(dst), dstType));
    }

    void processTackyStore(const TackyStore &tackyStore,
                           std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto ptr = tackyValToOperand(tackyStore.dstPtr);
        auto src = tackyValToOperand(tackyStore.src);
        auto srcType = assemblyTypeOf(tackyStore.src);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::move(ptr), std::make_unique<Register>(RegisterName::AX, 8),
            AssemblyType::QUADWORD));
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::move(src), std::make_unique<Memory>(RegisterName::AX, 0), srcType));
    }

    void processTackyGetAddress(const TackyGetAddress &tackyGetAddress,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyGetAddress.src);
        auto dst = tackyValToOperand(tackyGetAddress.dst);
        instructions.push_back(std::make_unique<LeaInstruction>(std::move(src), std::move(dst)));
    }

    void processTackyCopyToOffset(const TackyCopyToOffset &copy,
                                  std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(copy.src);
        const AssemblyType type = assemblyTypeOf(copy.src);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::move(src), std::make_unique<PseudoMem>(copy.dst, copy.offset, type), type));
    }

    void processTackyInstructions(
        const std::vector<std::unique_ptr<TackyInstruction>> &tackyInstructions,
        std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        for (const auto &instruction : tackyInstructions)
        {
            switch (instruction->getKind())
            {
            case TackyKind::Return:
            {
                processTackyReturn(*cast<TackyReturn>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Unary:
            {
                processTackyUnary(*cast<TackyUnary>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Binary:
            {
                processTackyBinary(*cast<TackyBinary>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Jump:
            {
                processTackyJump(*cast<TackyJump>(instruction.get()), instructions);
                break;
            }
            case TackyKind::JumpIfZero:
            {
                processTackyJumpIfZero(*cast<TackyJumpIfZero>(instruction.get()), instructions);
                break;
            }
            case TackyKind::JumpIfNotZero:
            {
                processTackyJumpIfNotZero(*cast<TackyJumpIfNotZero>(instruction.get()),
                                          instructions);
                break;
            }
            case TackyKind::Copy:
            {
                processTackyCopy(*cast<TackyCopy>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Label:
            {
                processTackyLabel(*cast<TackyLabel>(instruction.get()), instructions);
                break;
            }
            case TackyKind::FunctionCall:
            {
                processTackyFunctionCall(*cast<TackyFunctionCall>(instruction.get()), instructions);
                break;
            }
            case TackyKind::SignExtend:
            {
                processTackySignExtend(*cast<TackySignExtend>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Truncate:
            {
                processTackyTruncate(*cast<TackyTruncate>(instruction.get()), instructions);
                break;
            }
            case TackyKind::ZeroExtend:
            {
                processTackyZeroExtend(*cast<TackyZeroExtend>(instruction.get()), instructions);
                break;
            }
            case TackyKind::IntToDouble:
            {
                processTackyIntToDouble(*cast<TackyIntToDouble>(instruction.get()), instructions);
                break;
            }
            case TackyKind::DoubleToInt:
            {
                processTackyDoubleToInt(*cast<TackyDoubleToInt>(instruction.get()), instructions);
                break;
            }
            case TackyKind::UIntToDouble:
            {
                processTackyUIntToDouble(*cast<TackyUIntToDouble>(instruction.get()), instructions);
                break;
            }
            case TackyKind::DoubleToUInt:
            {
                processTackyDoubleToUInt(*cast<TackyDoubleToUInt>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Load:
            {
                processTackyLoad(*cast<TackyLoad>(instruction.get()), instructions);
                break;
            }
            case TackyKind::Store:
            {
                processTackyStore(*cast<TackyStore>(instruction.get()), instructions);
                break;
            }
            case TackyKind::GetAddress:
            {
                processTackyGetAddress(*cast<TackyGetAddress>(instruction.get()), instructions);
                break;
            }
            case TackyKind::CopyToOffset:
            {
                processTackyCopyToOffset(*cast<TackyCopyToOffset>(instruction.get()), instructions);
                break;
            }
            default:
                throw std::runtime_error("codegen: unhandled TACKY instruction kind");
            }
        }
    }

    std::unique_ptr<codegenFunction> processFunction(const TackyFunction &functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;

        curReturnsStruct = functionNode.returnsStruct;
        curReturnABI = functionNode.returnABI;
        const bool returnsMemory = functionNode.returnsStruct && functionNode.returnABI.inMemory;
        curRetPtrName = ".retptr." + functionNode.name;
        // A MEMORY-class struct return arrives as a hidden destination pointer in
        // RDI; stash it for the return statement, then real params start at RSI.
        if (returnsMemory)
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Register>(RegisterName::DI, 8),
                std::make_unique<PseudoRegister>(curRetPtrName, AssemblyType::QUADWORD),
                AssemblyType::QUADWORD));

        std::vector<Arg> params;
        params.reserve(functionNode.params.size());
        for (const auto &param : functionNode.params)
        {
            if (!param.structTag.empty())
            {
                Arg a;
                a.isStruct = true;
                a.name = param.name;
                a.structTag = param.structTag;
                a.abi = classifyStruct(param.structTag);
                params.push_back(std::move(a));
            }
            else
            {
                Arg a;
                a.name = param.name; // the param's pseudo-register, used as the slot dest
                a.val = TackyVar(param.name, param.type);
                a.stype = toAssemblyType(param.type);
                a.sdouble = isDouble(param.type);
                params.push_back(std::move(a));
            }
        }

        int stackCount = 0;
        auto placement = abi_.computeSlots(params, returnsMemory, stackCount);
        int stackIdx = 0;
        for (size_t i = 0; i < params.size(); i++)
        {
            const Arg &a = params[i];
            for (const auto &s : placement[i])
            {
                // Destination: a scalar param's pseudo-register, or a struct param's
                // slot eightbyte.
                auto dest = [&]() -> std::unique_ptr<Operand>
                {
                    if (a.isStruct)
                        return aggMem(a.name, s.objOff,
                                      s.where == Slot::SSE_REG ? AssemblyType::DOUBLE
                                                               : AssemblyType::QUADWORD,
                                      a.structTag, a.isStatic);
                    return std::make_unique<PseudoRegister>(a.name, a.stype);
                };
                if (s.where == Slot::STACK)
                {
                    const AssemblyType t = a.isStruct ? AssemblyType::QUADWORD : a.stype;
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        std::make_unique<Memory>(RegisterName::BP, 16 + 8 * stackIdx++), dest(),
                        t));
                }
                else if (s.where == Slot::SSE_REG)
                {
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        std::make_unique<Register>(s.reg, 8), dest(), AssemblyType::DOUBLE));
                }
                else
                {
                    const AssemblyType t = a.isStruct ? AssemblyType::QUADWORD : a.stype;
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        std::make_unique<Register>(s.reg, bytesOf(t)), dest(), t));
                }
            }
        }

        processTackyInstructions(functionNode.instructions, instructions);
        return std::make_unique<codegenFunction>(functionNode.line, functionNode.column,
                                                 functionNode.name, functionNode.global,
                                                 std::move(instructions));
    }

    // The object name and self-typing fields a pseudo operand carries. Returns false
    // for non-pseudo operands (immediates, real registers, already-lowered memory).
    static bool pseudoSelfType(const Operand &op, std::string &name, ObjInfo &info)
    {
        if (auto *p = dynamic_cast<const PseudoRegister *>(&op))
        {
            name = p->name;
            info = {p->isStatic, p->objSize, p->objAlign, p->structTag};
            return true;
        }
        if (auto *m = dynamic_cast<const PseudoMem *>(&op))
        {
            name = m->name;
            info = {m->isStatic, m->objSize, m->objAlign, m->structTag};
            return true;
        }
        return false;
    }

    void lowerPseudoSlot(std::unique_ptr<Operand> &slot,
                         std::unordered_map<std::string, int> &pseudoToOffset, int &used,
                         const std::unordered_map<std::string, ObjInfo> &objInfo)
    {
        // A pseudo register (scalar) or a pseudo-mem (an aggregate referenced at a
        // byte offset). Both resolve to the same per-name stack slot.
        std::string name;
        int extra = 0;
        AssemblyType type = AssemblyType::LONGWORD;
        if (auto *p = dynamic_cast<PseudoRegister *>(slot.get()))
        {
            name = p->name;
            type = p->type;
        }
        else if (auto *m = dynamic_cast<PseudoMem *>(slot.get()))
        {
            name = m->name;
            extra = m->offset;
            type = m->type;
        }
        else
            return;

        const ObjInfo *info = nullptr;
        if (auto it = objInfo.find(name); it != objInfo.end())
            info = &it->second;

        // A static-storage object lives in RIP-relative data, not a stack slot.
        if (info && info->isStatic)
        {
            slot = std::make_unique<Data>(name, extra);
            return;
        }

        auto found = pseudoToOffset.find(name);
        if (found == pseudoToOffset.end())
        {
            if (info && !info->structTag.empty())
            {
                // A struct slot is sized/aligned from its classification, but its size
                // is rounded up to a multiple of 8 so whole-eightbyte loads/stores
                // (used by the calling convention) never spill past the slot.
                const StructABI abi = classifyStruct(info->structTag);
                const int sz = (static_cast<int>(abi.size) + 7) / 8 * 8;
                used += sz;
                const int al = abi.align < 8 ? 8 : abi.align;
                used += (al - used % al) % al;
            }
            else if (info && info->objSize)
            {
                // Reserve the whole array, then align the base (-used) to the
                // object's alignment.
                used += static_cast<int>(info->objSize);
                used += (info->objAlign - used % info->objAlign) % info->objAlign;
            }
            else if (type == AssemblyType::QUADWORD || type == AssemblyType::DOUBLE)
            {
                used += (8 - used % 8) % 8; // align the slot to 8 bytes
                used += 8;
            }
            else if (type == AssemblyType::BYTE)
            {
                used += 1;
            }
            else
            {
                used += 4;
            }
            found = pseudoToOffset.emplace(name, -used).first;
        }
        slot = std::make_unique<Memory>(RegisterName::BP, found->second + extra);
    }

    void removePseudosFromFunction(codegenFunction &func)
    {
        // Pass 1: recover each named object's facts (static storage, aggregate size)
        // by merging the self-typing fields from every operand that references it.
        // Merging makes slot sizing order-independent — one carrying reference per
        // object suffices even if other references to it carry nothing.
        std::unordered_map<std::string, ObjInfo> objInfo;
        for (auto &instruction : func.instructions)
            forEachSlot(*instruction,
                        [&](std::unique_ptr<Operand> &op)
                        {
                            std::string name;
                            ObjInfo st;
                            if (!pseudoSelfType(*op, name, st))
                                return;
                            ObjInfo &e = objInfo[name];
                            if (st.isStatic)
                                e.isStatic = true;
                            if (st.objSize)
                            {
                                e.objSize = st.objSize;
                                e.objAlign = st.objAlign;
                            }
                            if (!st.structTag.empty())
                                e.structTag = st.structTag;
                        });

        // Pass 2: assign each pseudo to its stack slot (or RIP data, for statics).
        std::unordered_map<std::string, int> pseudoToOffset;
        int used = 0;
        for (auto &instruction : func.instructions)
            forEachSlot(*instruction, [&](std::unique_ptr<Operand> &op)
                        { lowerPseudoSlot(op, pseudoToOffset, used, objInfo); });

        if (int frameSize = used; frameSize > 0)
        {
            frameSize = frameSize + (16 - frameSize % 16) % 16;
            func.stackAllocation =
                std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(frameSize),
                                                    std::make_unique<Register>(RegisterName::SP, 8),
                                                    BinaryOp::Subtract, AssemblyType::QUADWORD);
        }
    }

    void removePseudos(codegenProgram &prog)
    {
        for (auto &node : prog.nodes)
        {
            if (auto *p = dynamic_cast<codegenFunction *>(node.get()))
            {
                removePseudosFromFunction(*p);
            }
        }
    }

    std::unique_ptr<codegenStaticVariable>
    processStaticVariable(const TackyStaticVariable &variable)
    {
        return std::make_unique<codegenStaticVariable>(variable.line, variable.column,
                                                       variable.identifier, variable.global,
                                                       variable.inits, variable.align);
    }

    std::unique_ptr<codegenProgram> codegen(const TackyProgram &prog)
    {
        std::vector<std::unique_ptr<codegenTopLevelNode>> nodes;
        for (const auto &node : prog.nodes)
        {
            if (auto *p = dynamic_cast<TackyFunction *>(node.get()))
            {
                nodes.push_back(processFunction(*p));
            }
            else if (auto *p = dynamic_cast<TackyStaticVariable *>(node.get()))
            {
                nodes.push_back(processStaticVariable(*p));
            }
        }

        auto codegenAST =
            std::make_unique<codegenProgram>(prog.line, prog.column, std::move(nodes));

        RegisterAllocator registerAllocator;
        registerAllocator.buildGraph(*codegenAST);

        removePseudos(*codegenAST);
        for (auto &node : codegenAST->nodes)
            if (auto *p = dynamic_cast<codegenFunction *>(node.get()))
                abi_.legalize(p->instructions);

        for (auto &c : constPool)
            codegenAST->nodes.push_back(std::move(c));

        return codegenAST;
    }
};