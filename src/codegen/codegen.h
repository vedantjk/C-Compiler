#pragma once

#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "ast/TopLevelNodes/codegenStaticConstant.h"
#include "ast/TopLevelNodes/codegenStaticVariable.h"
#include "instructions/instructions.h"
#include "tacky/ast/TopLevelNodes/TackyStaticVariable.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
class codegenDriver
{
    const std::vector<RegisterName> argRegisters{(RegisterName::DI), (RegisterName::SI),
                                                 (RegisterName::DX), (RegisterName::CX),
                                                 (RegisterName::R8), (RegisterName::R9)};
    const std::vector<RegisterName> xmmRegisters{
        (RegisterName::XMM0), (RegisterName::XMM1), (RegisterName::XMM2), (RegisterName::XMM3),
        (RegisterName::XMM4), (RegisterName::XMM5), (RegisterName::XMM6), (RegisterName::XMM7)};
    // Names of all static-storage variables (file-scope, block static, extern),
    // supplied by TACKY. Their pseudos lower to RIP-relative Data, not stack slots.
    std::unordered_set<std::string> staticNames;
    // Array objects (by name) that need a sized, aligned stack slot; supplied by TACKY.
    std::unordered_map<std::string, ArrayObject> arrayObjects;

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
            return std::make_unique<PseudoRegister>(v->name, assemblyTypeOf(t));
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
        if (tackyReturn.val)
        {
            const AssemblyType t = assemblyTypeOf(*tackyReturn.val);
            std::unique_ptr<Operand> source = tackyValToOperand(*tackyReturn.val);
            const bool dbl = t == AssemblyType::DOUBLE;
            auto dest =
                std::make_unique<Register>(dbl ? RegisterName::XMM0 : RegisterName::AX, bytesOf(t));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(source), std::move(dest), t));
        }
        instructions.push_back(std::make_unique<ReturnInstruction>());
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

    void processTackyFunctionCall(const TackyFunctionCall &tackyFunctionCall,
                                  std::vector<std::unique_ptr<Instruction>> &instructions)
    {

        std::vector<TackyVal> gpArgs, xmmArgs, stackArgs;

        int gpIndex = 0, xmmIndex = 0;
        for (const auto &arg : tackyFunctionCall.args)
        {
            if (typeOf(arg) == ConstantType::DOUBLE)
            {
                if (xmmIndex < 8)
                {
                    xmmArgs.push_back(arg);
                    xmmIndex++;
                }
                else
                    stackArgs.push_back(arg);
            }
            else if (gpIndex < 6)
            {
                gpArgs.push_back(arg);
                gpIndex++;
            }
            else
                stackArgs.push_back(arg);
        }

        int stackPadding = 0;
        if (static_cast<int>(stackArgs.size()) % 2)
        {
            stackPadding = 8;
        }

        if (stackPadding != 0)
        {
            instructions.push_back(
                std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(stackPadding),
                                                    std::make_unique<Register>(RegisterName::SP, 8),
                                                    BinaryOp::Subtract, AssemblyType::QUADWORD));
        }

        int regIndex = 0;
        for (auto &tackyArg : gpArgs)
        {
            const AssemblyType t = assemblyTypeOf(tackyArg);
            std::unique_ptr<Operand> r =
                std::make_unique<Register>(argRegisters[regIndex], bytesOf(t));
            auto assemblyArg = tackyValToOperand(tackyArg);
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(assemblyArg), std::move(r), t));
            regIndex++;
        }

        regIndex = 0;
        for (auto &tackyArg : xmmArgs)
        {
            const AssemblyType t = assemblyTypeOf(tackyArg);
            std::unique_ptr<Operand> r =
                std::make_unique<Register>(xmmRegisters[regIndex], bytesOf(t));
            auto assemblyArg = tackyValToOperand(tackyArg);
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(assemblyArg), std::move(r), t));
            regIndex++;
        }

        for (auto it = stackArgs.rbegin(); it != stackArgs.rend(); ++it)
        {
            auto &tackyArg = *it;
            auto assemblyArg = tackyValToOperand(tackyArg);
            // Immediates and quadwords can be pushed directly (pushq is 8-byte).
            // A longword in memory must go via %eax so we push a full 8-byte slot.
            if (isImmediate(*assemblyArg) || assemblyTypeOf(tackyArg) == AssemblyType::QUADWORD ||
                assemblyTypeOf(tackyArg) == AssemblyType::DOUBLE)
            {
                instructions.push_back(std::make_unique<PushInstruction>(std::move(assemblyArg)));
            }
            else
            {
                // A sub-quadword arg (longword/byte): move it into AX at its own
                // width, then push the full 8-byte slot.
                const AssemblyType at = assemblyTypeOf(tackyArg);
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::move(assemblyArg),
                    std::make_unique<Register>(RegisterName::AX, bytesOf(at)), at));
                instructions.push_back(std::make_unique<PushInstruction>(
                    std::make_unique<Register>(RegisterName::AX, 8)));
            }
        }

        if (tackyFunctionCall.variadic)
        {
            // SysV: a variadic callee expects %al = number of vector (XMM) registers
            // used to pass args. movl into %eax sets %al; the upper bits don't matter.
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Immediate>(static_cast<long long>(xmmArgs.size())),
                std::make_unique<Register>(RegisterName::AX, 4), AssemblyType::LONGWORD));
        }
        instructions.push_back(std::make_unique<CallInstruction>(tackyFunctionCall.funcName));

        if (int bytesToRemove = 8 * static_cast<int>(stackArgs.size()) + stackPadding)
        {
            instructions.push_back(
                std::make_unique<BinaryInstruction>(std::make_unique<Immediate>(bytesToRemove),
                                                    std::make_unique<Register>(RegisterName::SP, 8),
                                                    BinaryOp::Add, AssemblyType::QUADWORD));
        }

        const AssemblyType retType = assemblyTypeOf(tackyFunctionCall.dst);
        auto assemblyDst = tackyValToOperand(tackyFunctionCall.dst);
        if (retType == AssemblyType::DOUBLE)
        {
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Register>(RegisterName::XMM0, bytesOf(retType)),
                std::move(assemblyDst), retType));
        }
        else
        {
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Register>(RegisterName::AX, bytesOf(retType)),
                std::move(assemblyDst), retType));
        }
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
            if (auto *p = dynamic_cast<TackyReturn *>(instruction.get()))
            {
                processTackyReturn(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyUnary *>(instruction.get()))
            {
                processTackyUnary(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyBinary *>(instruction.get()))
            {
                processTackyBinary(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyJump *>(instruction.get()))
            {
                processTackyJump(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyJumpIfZero *>(instruction.get()))
            {
                processTackyJumpIfZero(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyJumpIfNotZero *>(instruction.get()))
            {
                processTackyJumpIfNotZero(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyCopy *>(instruction.get()))
            {
                processTackyCopy(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyLabel *>(instruction.get()))
            {
                processTackyLabel(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyFunctionCall *>(instruction.get()))
            {
                processTackyFunctionCall(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackySignExtend *>(instruction.get()))
            {
                processTackySignExtend(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyTruncate *>(instruction.get()))
            {
                processTackyTruncate(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyZeroExtend *>(instruction.get()))
            {
                processTackyZeroExtend(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyIntToDouble *>(instruction.get()))
            {
                processTackyIntToDouble(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyDoubleToInt *>(instruction.get()))
            {
                processTackyDoubleToInt(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyUIntToDouble *>(instruction.get()))
            {
                processTackyUIntToDouble(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyDoubleToUInt *>(instruction.get()))
            {
                processTackyDoubleToUInt(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyLoad *>(instruction.get()))
            {
                processTackyLoad(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyStore *>(instruction.get()))
            {
                processTackyStore(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyGetAddress *>(instruction.get()))
            {
                processTackyGetAddress(*p, instructions);
            }
            else if (auto *p = dynamic_cast<TackyCopyToOffset *>(instruction.get()))
            {
                processTackyCopyToOffset(*p, instructions);
            }
            else
            {
                throw std::runtime_error("codegen: unhandled TACKY instruction kind");
            }
        }
    }

    std::unique_ptr<codegenFunction> processFunction(const TackyFunction &functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;

        // SysV: integer params arrive in RDI.. (6 slots), double params in XMM0..
        // (8 slots), each bank counted independently. Params that overflow their
        // bank were pushed by the caller and sit at 16(%rbp), 24(%rbp), ... in the
        // order they appear.
        int gpIndex = 0, xmmIndex = 0, stackOffset = 16;
        for (const auto &[pname, pct] : functionNode.params)
        {
            const AssemblyType pt = toAssemblyType(pct);
            if (isDouble(pct))
            {
                if (xmmIndex < 8)
                {
                    instructions.push_back(std::make_unique<MoveInstruction>(
                        std::make_unique<Register>(xmmRegisters[xmmIndex], 8),
                        std::make_unique<PseudoRegister>(pname, pt), AssemblyType::DOUBLE));
                    xmmIndex++;
                    continue;
                }
            }
            else if (gpIndex < 6)
            {
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::make_unique<Register>(argRegisters[gpIndex], bytesOf(pt)),
                    std::make_unique<PseudoRegister>(pname, pt), pt));
                gpIndex++;
                continue;
            }

            // Overflowed its register bank: read from the caller's stack frame.
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Memory>(RegisterName::BP, stackOffset),
                std::make_unique<PseudoRegister>(pname, pt), pt));
            stackOffset += 8;
        }

        processTackyInstructions(functionNode.instructions, instructions);
        return std::make_unique<codegenFunction>(functionNode.line, functionNode.column,
                                                 functionNode.name, functionNode.global,
                                                 std::move(instructions));
    }

    void lowerPseudoSlot(std::unique_ptr<Operand> &slot,
                         std::unordered_map<std::string, int> &pseudoToOffset, int &used,
                         const std::unordered_set<std::string> &staticNames)
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

        if (staticNames.count(name))
        {
            slot = std::make_unique<Data>(name);
            return;
        }

        auto found = pseudoToOffset.find(name);
        if (found == pseudoToOffset.end())
        {
            if (auto ai = arrayObjects.find(name); ai != arrayObjects.end())
            {
                // Reserve the whole array, then align the base (-used) to the
                // object's alignment.
                used += static_cast<int>(ai->second.size);
                used += (ai->second.align - used % ai->second.align) % ai->second.align;
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

    void removePseudosFromFunction(codegenFunction &func,
                                   std::unordered_set<std::string> &staticNames)
    {
        std::unordered_map<std::string, int> pseudoToOffset;
        int used = 0;
        for (auto &instruction : func.instructions)
        {
            if (auto *p = dynamic_cast<MoveInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<MoveSXInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<MoveZeroExtendInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<UnaryInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<BinaryInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<IDivInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<DivInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<CmpInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->b, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<SetCCInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<PushInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<CVTSI2SD *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<CVTTSD2SI *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
            else if (auto *p = dynamic_cast<LeaInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, used, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, used, staticNames);
            }
        }
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
                removePseudosFromFunction(*p, staticNames);
            }
        }
    }

    bool isData(const Operand &op) { return dynamic_cast<const Data *>(&op) != nullptr; }
    bool isMemory(const Operand &op)
    {
        return dynamic_cast<const Memory *>(&op) != nullptr || isData(op);
    }

    // Clone a memory operand (Memory or Data) — used when a fixup needs two copies
    // of an instruction's memory destination.
    std::unique_ptr<Operand> cloneMemory(const Operand *op)
    {
        if (auto *d = dynamic_cast<const Data *>(op))
            return std::make_unique<Data>(*d);
        if (auto *m = dynamic_cast<const Memory *>(op))
            return std::make_unique<Memory>(*m);
        return nullptr;
    }

    // An immediate that does not fit in a signed 32-bit field; such values can't be
    // an operand of most instructions and must be staged in a register first.
    static bool isLargeImmediate(const Operand &op)
    {
        auto *i = dynamic_cast<const Immediate *>(&op);
        return i && (i->value > 2147483647LL || i->value < -2147483648LL);
    }

    static std::unique_ptr<Register> reg(const RegisterName n, const int bytes)
    {
        return std::make_unique<Register>(n, bytes);
    }

    void fixupInstructionsFromFunction(codegenFunction &func)
    {
        std::vector<std::unique_ptr<Instruction>> rewritten;
        rewritten.reserve(func.instructions.size());

        for (auto &instr : func.instructions)
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

        func.instructions = std::move(rewritten);
    }

    void fixupInstructions(codegenProgram &prog)
    {
        for (auto &node : prog.nodes)
        {
            if (auto *p = dynamic_cast<codegenFunction *>(node.get()))
            {
                fixupInstructionsFromFunction(*p);
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
        staticNames = prog.staticNames;
        arrayObjects = prog.arrayObjects;
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

        removePseudos(*codegenAST);
        fixupInstructions(*codegenAST);

        for (auto &c : constPool)
            codegenAST->nodes.push_back(std::move(c));

        return codegenAST;
    }
};