#pragma once

#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "ast/TopLevelNodes/codegenStaticVariable.h"
#include "instructions/instructions.h"
#include "tacky/ast/TopLevelNodes/TackyStaticVariable.h"

#include <memory>
#include <unordered_map>
#include <unordered_set>
class codegenDriver
{
    const std::vector<RegisterName> argRegisters{(RegisterName::DI), (RegisterName::SI),
                                                 (RegisterName::DX), (RegisterName::CX),
                                                 (RegisterName::R8), (RegisterName::R9)};
    // Names of all static-storage variables (file-scope, block static, extern),
    // supplied by TACKY. Their pseudos lower to RIP-relative Data, not stack slots.
    std::unordered_set<std::string> staticNames;

  public:
    static bool isImmediate(const Operand &op)
    {
        return dynamic_cast<const Immediate *>(&op) != nullptr;
    }

    // Assembly width of a TACKY value: long -> 8-byte quadword, else 4-byte longword.
    static AssemblyType toAssemblyType(const ConstantType t)
    {
        return ctBytes(t) == 8 ? AssemblyType::QUADWORD : AssemblyType::LONGWORD;
    }
    static AssemblyType assemblyTypeOf(const TackyVal &v) { return toAssemblyType(typeOf(v)); }
    // Register width that matches an assembly type.
    static int bytesOf(const AssemblyType t) { return t == AssemblyType::QUADWORD ? 8 : 4; }

    std::unique_ptr<Operand> tackyValToOperand(const TackyVal &t)
    {
        if (auto *c = std::get_if<TackyConstant>(&t))
        {
            return std::make_unique<Immediate>(c->value);
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

        if (tackyUnary.op == UnaryOp::Not)
        {
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
            std::unique_ptr<Operand> dest =
                std::make_unique<Register>(RegisterName::AX, bytesOf(t));
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
        if (tackyBinary.op == BinaryOp::Divide)
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
            // The comparison runs at the operands' (common) width and signedness;
            // the result is int.
            CondCode cc =
                binaryOpToCondCode(tackyBinary.op, isUnsignedCt(typeOf(tackyBinary.src1)));
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
        std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
        auto condition = tackyValToOperand(tackyJIZ.condition);
        instructions.push_back(std::make_unique<CmpInstruction>(
            std::move(imm1), std::move(condition), assemblyTypeOf(tackyJIZ.condition)));
        instructions.push_back(
            std::make_unique<JumpCCInstruction>(CondCode::E, tackyJIZ.identifier));
    }

    void processTackyJumpIfNotZero(const TackyJumpIfNotZero &tackyJIZ,
                                   std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
        auto condition = tackyValToOperand(tackyJIZ.condition);
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

        std::vector<TackyVal> registerArgs, stackArgs;

        int argumentCount = 0;
        while (argumentCount < 6 && argumentCount < static_cast<int>(tackyFunctionCall.args.size()))
        {
            registerArgs.push_back(tackyFunctionCall.args[argumentCount]);
            argumentCount++;
        }

        for (int i = static_cast<int>(tackyFunctionCall.args.size()) - 1; i >= argumentCount; i--)
        {
            stackArgs.push_back(tackyFunctionCall.args[i]);
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
        for (auto &tackyArg : registerArgs)
        {
            const AssemblyType t = assemblyTypeOf(tackyArg);
            std::unique_ptr<Operand> r =
                std::make_unique<Register>(argRegisters[regIndex], bytesOf(t));
            auto assemblyArg = tackyValToOperand(tackyArg);
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(assemblyArg), std::move(r), t));
            regIndex++;
        }

        for (auto &tackyArg : stackArgs)
        {
            auto assemblyArg = tackyValToOperand(tackyArg);
            // Immediates and quadwords can be pushed directly (pushq is 8-byte).
            // A longword in memory must go via %eax so we push a full 8-byte slot.
            if (isImmediate(*assemblyArg) || assemblyTypeOf(tackyArg) == AssemblyType::QUADWORD)
            {
                instructions.push_back(std::make_unique<PushInstruction>(std::move(assemblyArg)));
            }
            else
            {
                instructions.push_back(std::make_unique<MoveInstruction>(
                    std::move(assemblyArg), std::make_unique<Register>(RegisterName::AX, 4)));
                instructions.push_back(std::make_unique<PushInstruction>(
                    std::make_unique<Register>(RegisterName::AX, 8)));
            }
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
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Register>(RegisterName::AX, bytesOf(retType)), std::move(assemblyDst),
            retType));
    }

    void processTackySignExtend(const TackySignExtend &tackySignExtend,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackySignExtend.src);
        auto dst = tackyValToOperand(tackySignExtend.dst);
        instructions.push_back(std::make_unique<MoveSXInstruction>(std::move(src), std::move(dst)));
    }

    void processTackyZeroExtend(const TackyZeroExtend &tackyZeroExtend,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyZeroExtend.src);
        auto dst = tackyValToOperand(tackyZeroExtend.dst);
        instructions.push_back(
            std::make_unique<MoveZeroExtendInstruction>(std::move(src), std::move(dst)));
    }

    void processTackyTruncate(const TackyTruncate &tackyTruncate,
                              std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        // Truncation is a plain 4-byte move: it keeps the low 32 bits of the source.
        auto src = tackyValToOperand(tackyTruncate.src);
        auto dst = tackyValToOperand(tackyTruncate.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src), std::move(dst),
                                                                 AssemblyType::LONGWORD));
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
        }
    }

    std::unique_ptr<codegenFunction> processFunction(const TackyFunction &functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;
        int regIndex = 0;
        while (regIndex < 6 && regIndex < static_cast<int>(functionNode.params.size()))
        {
            const auto &[pname, pct] = functionNode.params[regIndex];
            const AssemblyType pt = toAssemblyType(pct);
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Register>(argRegisters[regIndex], bytesOf(pt)),
                std::make_unique<PseudoRegister>(pname, pt), pt));
            regIndex++;
        }
        for (int i = 6; i < static_cast<int>(functionNode.params.size()); i++)
        {
            const auto &[pname, pct] = functionNode.params[i];
            const AssemblyType pt = toAssemblyType(pct);
            int offset = 16 + 8 * (i - 6);
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Stack>(offset), std::make_unique<PseudoRegister>(pname, pt), pt));
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
        auto *p = dynamic_cast<PseudoRegister *>(slot.get());
        if (!p)
            return;
        if (staticNames.count(p->name))
        {
            slot = std::make_unique<Data>(p->name);
            return;
        }
        auto found = pseudoToOffset.find(p->name);
        if (found == pseudoToOffset.end())
        {
            if (p->type == AssemblyType::QUADWORD)
            {
                used += (8 - used % 8) % 8; // align the slot to 8 bytes
                used += 8;
            }
            else
            {
                used += 4;
            }
            found = pseudoToOffset.emplace(p->name, -used).first;
        }
        slot = std::make_unique<Stack>(found->second);
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

    bool isStack(const Operand &op) { return dynamic_cast<const Stack *>(&op) != nullptr; }
    bool isData(const Operand &op) { return dynamic_cast<const Data *>(&op) != nullptr; }
    bool isMemory(const Operand &op) { return isStack(op) || isData(op); }

    // Clone a memory operand (Stack or Data) — used when a fixup needs two copies
    // of an instruction's memory destination.
    std::unique_ptr<Operand> cloneMemory(const Operand *op)
    {
        if (auto *s = dynamic_cast<const Stack *>(op))
            return std::make_unique<Stack>(*s);
        if (auto *d = dynamic_cast<const Data *>(op))
            return std::make_unique<Data>(*d);
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
                // A 64-bit immediate or a memory source can't move straight to
                // memory; stage it in R10 first.
                if (isMemory(*m->dst) && (isMemory(*m->src) || isLargeImmediate(*m->src)))
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
                // movslq needs a non-immediate source and a register destination.
                auto src = std::move(m->src);
                auto dst = std::move(m->dst);
                if (isImmediate(*src))
                {
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        std::move(src), reg(RegisterName::R10, 4), AssemblyType::LONGWORD));
                    src = reg(RegisterName::R10, 4);
                }
                if (isMemory(*dst))
                {
                    rewritten.push_back(std::make_unique<MoveSXInstruction>(
                        std::move(src), reg(RegisterName::R11, 8)));
                    rewritten.push_back(std::make_unique<MoveInstruction>(
                        reg(RegisterName::R11, 8), std::move(dst), AssemblyType::QUADWORD));
                }
                else
                {
                    rewritten.push_back(
                        std::make_unique<MoveSXInstruction>(std::move(src), std::move(dst)));
                }
            }
            else if (auto *m = dynamic_cast<MoveZeroExtendInstruction *>(instr.get()))
            {
                // No movzlq exists: zero-extending 32->64 is a plain 32-bit mov,
                // which clears the upper half of a register destination. A memory
                // destination can't auto-zero its upper word, so route through R11.
                auto src = std::move(m->src);
                auto dst = std::move(m->dst);
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
            else if (auto *m = dynamic_cast<PushInstruction *>(instr.get());
                     m && isLargeImmediate(*m->a))
            {
                // pushq takes a register/memory/32-bit-immediate, not a 64-bit one.
                rewritten.push_back(std::make_unique<MoveInstruction>(
                    std::move(m->a), reg(RegisterName::R10, 8), AssemblyType::QUADWORD));
                rewritten.push_back(std::make_unique<PushInstruction>(reg(RegisterName::R10, 8)));
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
                                                       variable.init, variable.type);
    }

    std::unique_ptr<codegenProgram> codegen(const TackyProgram &prog)
    {
        staticNames = prog.staticNames;
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
        return codegenAST;
    }
};