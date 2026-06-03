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
    static AssemblyType assemblyTypeOf(const TackyVal &v)
    {
        return typeOf(v) == ConstantType::LONG ? AssemblyType::QUADWORD : AssemblyType::LONGWORD;
    }

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

        if (tackyUnary.op == UnaryOp::Not)
        {
            std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
            std::unique_ptr<Operand> imm2 = std::make_unique<Immediate>(0);
            instructions.push_back(
                std::make_unique<CmpInstruction>(std::move(imm1), std::move(src)));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(imm2), std::move(dst1)));
            instructions.push_back(
                std::make_unique<SetCCInstruction>(CondCode::E, std::move(dst2)));
            return;
        }

        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src), std::move(dst1)));
        instructions.push_back(std::make_unique<UnaryInstruction>(std::move(dst2), tackyUnary.op));
    }

    void processTackyReturn(const TackyReturn &tackyReturn,
                            std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        if (tackyReturn.val)
        {
            std::unique_ptr<Operand> source = tackyValToOperand(*tackyReturn.val);
            std::unique_ptr<Operand> dest = std::make_unique<Register>(RegisterName::AX, 4);
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(source), std::move(dest)));
        }
        instructions.push_back(std::make_unique<ReturnInstruction>());
    }

    void processDivision(const TackyBinary &tackyBinary,
                         std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAXa = std::make_unique<Register>(RegisterName::AX, 4);
        std::unique_ptr<Operand> regAXb = std::make_unique<Register>(RegisterName::AX, 4);
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src1), std::move(regAXa)));
        instructions.push_back(std::make_unique<CdqInstruction>());
        instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2)));
        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(regAXb), std::move(dst)));
    }

    void processRemainder(const TackyBinary &tackyBinary,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAX = std::make_unique<Register>(RegisterName::AX, 4);
        std::unique_ptr<Operand> regDX = std::make_unique<Register>(RegisterName::DX, 4);
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(
            std::make_unique<MoveInstruction>(std::move(src1), std::move(regAX)));
        instructions.push_back(std::make_unique<CdqInstruction>());
        instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2)));
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(regDX), std::move(dst)));
    }

    static bool isRelationalOp(const BinaryOp op)
    {
        return op == BinaryOp::Equal || op == BinaryOp::NotEqual || op == BinaryOp::LessThan ||
               op == BinaryOp::LessOrEqual || op == BinaryOp::GreaterThan ||
               op == BinaryOp::GreaterThanOrEqual;
    }

    static CondCode binaryOpToCondCode(const BinaryOp op)
    {
        if (op == BinaryOp::Equal)
            return CondCode::E;
        if (op == BinaryOp::NotEqual)
            return CondCode::NE;
        if (op == BinaryOp::LessThan)
            return CondCode::L;
        if (op == BinaryOp::LessOrEqual)
            return CondCode::LE;
        if (op == BinaryOp::GreaterThan)
            return CondCode::G;
        if (op == BinaryOp::GreaterThanOrEqual)
            return CondCode::GE;

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

        if (isRelationalOp(tackyBinary.op))
        {
            CondCode cc = binaryOpToCondCode(tackyBinary.op);
            std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
            instructions.push_back(
                std::make_unique<CmpInstruction>(std::move(src2), std::move(src1)));
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(imm1), std::move(dst1)));
            instructions.push_back(std::make_unique<SetCCInstruction>(cc, std::move(dst2)));
            return;
        }

        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src1), std::move(dst1)));
        instructions.push_back(
            std::make_unique<BinaryInstruction>(std::move(src2), std::move(dst2), tackyBinary.op));
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
        instructions.push_back(
            std::make_unique<CmpInstruction>(std::move(imm1), std::move(condition)));
        instructions.push_back(
            std::make_unique<JumpCCInstruction>(CondCode::E, tackyJIZ.identifier));
    }

    void processTackyJumpIfNotZero(const TackyJumpIfNotZero &tackyJIZ,
                                   std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        std::unique_ptr<Operand> imm1 = std::make_unique<Immediate>(0);
        auto condition = tackyValToOperand(tackyJIZ.condition);
        instructions.push_back(
            std::make_unique<CmpInstruction>(std::move(imm1), std::move(condition)));
        instructions.push_back(
            std::make_unique<JumpCCInstruction>(CondCode::NE, tackyJIZ.identifier));
    }

    void processTackyCopy(const TackyCopy &tackyCopy,
                          std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackyCopy.src);
        auto dst = tackyValToOperand(tackyCopy.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src), std::move(dst)));
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
            std::unique_ptr<Operand> r = std::make_unique<Register>(argRegisters[regIndex], 4);
            auto assemblyArg = tackyValToOperand(tackyArg);
            instructions.push_back(
                std::make_unique<MoveInstruction>(std::move(assemblyArg), std::move(r)));
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

        auto assemblyDst = tackyValToOperand(tackyFunctionCall.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(
            std::make_unique<Register>(RegisterName::AX, 4), std::move(assemblyDst)));
    }

    void processTackySignExtend(const TackySignExtend &tackySignExtend,
                                std::vector<std::unique_ptr<Instruction>> &instructions)
    {
        auto src = tackyValToOperand(tackySignExtend.src);
        auto dst = tackyValToOperand(tackySignExtend.dst);
        instructions.push_back(std::make_unique<MoveSXInstruction>(std::move(src), std::move(dst)));
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
        }
    }

    std::unique_ptr<codegenFunction> processFunction(const TackyFunction &functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;
        int regIndex = 0;
        while (regIndex < 6 && regIndex < static_cast<int>(functionNode.params.size()))
        {
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Register>(argRegisters[regIndex], 4),
                std::make_unique<PseudoRegister>(functionNode.params[regIndex])));
            regIndex++;
        }
        for (int i = 6; i < static_cast<int>(functionNode.params.size()); i++)
        {
            int offset = 16 + 8 * (i - 6);
            instructions.push_back(std::make_unique<MoveInstruction>(
                std::make_unique<Stack>(offset),
                std::make_unique<PseudoRegister>(functionNode.params[i])));
        }

        processTackyInstructions(functionNode.instructions, instructions);
        return std::make_unique<codegenFunction>(functionNode.line, functionNode.column,
                                                 functionNode.name, functionNode.global,
                                                 std::move(instructions));
    }

    void lowerPseudoSlot(std::unique_ptr<Operand> &slot,
                         std::unordered_map<std::string, int> &pseudoToOffset, int &nextOffset,
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
        auto [it, inserted] = pseudoToOffset.try_emplace(p->name, nextOffset);
        if (inserted)
            nextOffset -= 4;
        slot = std::make_unique<Stack>(it->second);
    }

    void removePseudosFromFunction(codegenFunction &func,
                                   std::unordered_set<std::string> &staticNames)
    {
        std::unordered_map<std::string, int> pseudoToOffset;
        int nextOffset = -4;
        for (auto &instruction : func.instructions)
        {
            if (auto *p = dynamic_cast<MoveInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, nextOffset, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<UnaryInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<BinaryInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, nextOffset, staticNames);
                lowerPseudoSlot(p->dst, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<IDivInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<CmpInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, nextOffset, staticNames);
                lowerPseudoSlot(p->b, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<SetCCInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, nextOffset, staticNames);
            }
            else if (auto *p = dynamic_cast<PushInstruction *>(instruction.get()))
            {
                lowerPseudoSlot(p->a, pseudoToOffset, nextOffset, staticNames);
            }
        }
        if (int frameSize = -nextOffset - 4; frameSize > 0)
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

    void fixupInstructionsFromFunction(codegenFunction &func)
    {
        std::vector<std::unique_ptr<Instruction>> rewritten;
        rewritten.reserve(func.instructions.size());

        for (auto &instr : func.instructions)
        {
            if (auto *m = dynamic_cast<MoveInstruction *>(instr.get());
                m && isMemory(*m->src) && isMemory(*m->dst))
            {
                // Mov M1, M2  →  Mov M1, R10 ; Mov R10, M2
                auto r10a = std::make_unique<Register>(RegisterName::R10, 4);
                auto r10b = std::make_unique<Register>(RegisterName::R10, 4);
                rewritten.push_back(
                    std::make_unique<MoveInstruction>(std::move(m->src), std::move(r10a)));
                rewritten.push_back(
                    std::make_unique<MoveInstruction>(std::move(r10b), std::move(m->dst)));
            }
            else if (auto *m = dynamic_cast<IDivInstruction *>(instr.get());
                     m && isImmediate(*m->operand))
            {
                std::unique_ptr<Operand> scratchRegister =
                    std::make_unique<Register>(RegisterName::R10, 4);
                m->scratchRegisterInstruction = std::make_unique<MoveInstruction>(
                    std::move(m->operand), std::make_unique<Register>(RegisterName::R10, 4));
                m->operand = std::move(scratchRegister);
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<BinaryInstruction *>(instr.get());
                     m &&
                     (m->op == BinaryOp::Add || m->op == BinaryOp::Subtract ||
                      m->op == BinaryOp::BitwiseAnd || m->op == BinaryOp::BitwiseOr ||
                      m->op == BinaryOp::BitwiseXor) &&
                     isMemory(*m->src) && isMemory(*m->dst))
            {
                // Mov M1, M2  →  Mov M1, R10 ; Mov R10, M2
                auto r10a = std::make_unique<Register>(RegisterName::R10, 4);
                auto r10b = std::make_unique<Register>(RegisterName::R10, 4);
                m->preStackFixInstruction =
                    std::make_unique<MoveInstruction>(std::move(m->src), std::move(r10a));
                m->src = std::move(r10b);
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<BinaryInstruction *>(instr.get());
                     m && (m->op == BinaryOp::Multiply) && isMemory(*m->dst))
            {
                auto dstcopy1 = cloneMemory(m->dst.get());
                auto dstcopy2 = cloneMemory(m->dst.get());
                auto r11a = std::make_unique<Register>(RegisterName::R11, 4);
                auto r11b = std::make_unique<Register>(RegisterName::R11, 4);
                auto r11c = std::make_unique<Register>(RegisterName::R11, 4);
                m->preStackFixInstruction =
                    std::make_unique<MoveInstruction>(std::move(dstcopy1), std::move(r11a));
                m->dst = std::move(r11b);
                m->postStackFixInstruction =
                    std::make_unique<MoveInstruction>(std::move(r11c), std::move(dstcopy2));
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<BinaryInstruction *>(instr.get());
                     m && (m->op == BinaryOp::LeftShift || m->op == BinaryOp::RightShift) &&
                     !isImmediate(*m->src))
            {
                auto ecxRegister = std::make_unique<Register>(RegisterName::CX, 4);
                auto clRegister = std::make_unique<Register>(RegisterName::CX, 1);
                m->preStackFixInstruction =
                    std::make_unique<MoveInstruction>(std::move(m->src), std::move(ecxRegister));
                m->src = std::move(clRegister);
                rewritten.push_back(std::move(instr));
            }
            else if (auto *m = dynamic_cast<CmpInstruction *>(instr.get()))
            {
                if (isMemory(*m->a) && isMemory(*m->b))
                {
                    auto r10a = std::make_unique<Register>(RegisterName::R10, 4);
                    auto r10b = std::make_unique<Register>(RegisterName::R10, 4);
                    m->preStackFixInstruction =
                        std::make_unique<MoveInstruction>(std::move(m->a), std::move(r10a));
                    m->a = std::move(r10b);
                }
                else if (isImmediate(*m->b))
                {
                    auto r11a = std::make_unique<Register>(RegisterName::R11, 4);
                    auto r11b = std::make_unique<Register>(RegisterName::R11, 4);
                    m->preStackFixInstruction =
                        std::make_unique<MoveInstruction>(std::move(m->b), std::move(r11a));
                    m->b = std::move(r11b);
                }
                rewritten.push_back(std::move(instr));
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
        return std::make_unique<codegenStaticVariable>(
            variable.line, variable.column, variable.identifier, variable.global, variable.init);
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