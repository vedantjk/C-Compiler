#pragma once

#include "../tacky/ast/ASTNodes/TackyProgram.h"
#include "../tacky/ast/TopLevelNodes/TackyFunction.h"
#include "instructions/instructions.h"
#include <memory>
#include <unordered_map>
class codegenDriver
{
    public:

    static bool isImmediate(const Operand& op)
    {
        return dynamic_cast<const Immediate*>(&op) != nullptr;
    }

    std::unique_ptr<Operand> tackyValToOperand(const TackyVal& t)
    {
        if (auto* c = std::get_if<TackyConstant>(&t))
        {
            return std::make_unique<Immediate>(c->value);
        }
         if (auto* v = std::get_if<TackyVar>(&t))
        {
            return std::make_unique<PseudoRegister>(v->name);
        }
        return nullptr;
    }

    void processTackyUnary(const TackyUnary& tackyUnary, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        auto src = tackyValToOperand(tackyUnary.src);
        auto dstForMove = tackyValToOperand(tackyUnary.dst);
        auto dstForUnary = tackyValToOperand(tackyUnary.dst);
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src), std::move(dstForMove)));
        instructions.push_back(std::make_unique<UnaryInstruction>(std::move(dstForUnary), tackyUnary.op));
    }

    void processTackyReturn(const TackyReturn& tackyReturn, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        if (tackyReturn.val)
        {
            std::unique_ptr<Operand> source = tackyValToOperand(*tackyReturn.val);
            std::unique_ptr<Operand> dest = std::make_unique<Register>(RegisterName::AX);
            instructions.push_back(std::make_unique<MoveInstruction>(std::move(source), std::move(dest)));
        }
        instructions.push_back(std::make_unique<ReturnInstruction>());
    }

    void processDivision(const TackyBinary& tackyBinary, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAXa = std::make_unique<Register>(RegisterName::AX);
        std::unique_ptr<Operand> regAXb = std::make_unique<Register>(RegisterName::AX);
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src1), std::move(regAXa)));
        instructions.push_back(std::make_unique<CdqInstruction>());
        instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2)));
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(regAXb), std::move(dst)));
    }

    void processRemainder(const TackyBinary& tackyBinary, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        auto src1 = tackyValToOperand(tackyBinary.src1);
        auto src2 = tackyValToOperand(tackyBinary.src2);
        std::unique_ptr<Operand> regAX = std::make_unique<Register>(RegisterName::AX);
        std::unique_ptr<Operand> regDX = std::make_unique<Register>(RegisterName::DX);
        auto dst = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src1), std::move(regAX)));
        instructions.push_back(std::make_unique<CdqInstruction>());
        instructions.push_back(std::make_unique<IDivInstruction>(std::move(src2)));
        instructions.push_back(std::make_unique<MoveInstruction>(std::move(regDX), std::move(dst)));
    }

    void processTackyBinary(const TackyBinary& tackyBinary, std::vector<std::unique_ptr<Instruction>>& instructions)
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
        auto dstForMove = tackyValToOperand(tackyBinary.dst);
        auto dstForBinary = tackyValToOperand(tackyBinary.dst);

        instructions.push_back(std::make_unique<MoveInstruction>(std::move(src1), std::move(dstForMove)));
        instructions.push_back(std::make_unique<BinaryInstruction>(std::move(src2), std::move(dstForBinary), tackyBinary.op));
    }

    void processTackyInstructions(const std::vector<std::unique_ptr<TackyInstruction>>& tackyInstructions, std::vector<std::unique_ptr<Instruction>>& instructions)
    {
        for (const auto& instruction : tackyInstructions)
        {
            if (auto* p = dynamic_cast<TackyReturn*>(instruction.get()))
            {
                processTackyReturn(*p, instructions);
            }
            else if (auto* p = dynamic_cast<TackyUnary*>(instruction.get()))
            {
                processTackyUnary(*p, instructions);
            }
            else if (auto* p = dynamic_cast<TackyBinary*>(instruction.get()))
            {
                processTackyBinary(*p, instructions);
            }
        }
    }

    std::unique_ptr<codegenFunction> processFunction(const TackyFunction& functionNode)
    {
        std::vector<std::unique_ptr<Instruction>> instructions;
        processTackyInstructions(functionNode.instructions, instructions);
        return std::make_unique<codegenFunction>(functionNode.line, functionNode.column, functionNode.name, std::move(instructions));
    }

    void lowerPseudoSlot(std::unique_ptr<Operand>& slot,
                       std::unordered_map<std::string, int>& pseudoToOffset,
                       int& nextOffset)
    {
        auto* p = dynamic_cast<PseudoRegister*>(slot.get());
        if (!p) return;   // not a pseudo, nothing to do

        auto [it, inserted] = pseudoToOffset.try_emplace(p->name, nextOffset);
        if (inserted) nextOffset -= 4;

        slot = std::make_unique<Stack>(it->second);
    }

    void removePseudosFromFunction(codegenFunction& func)
    {
        std::unordered_map<std::string, int> pseudoToOffset;
        int nextOffset = -4;
        for (auto& instruction : func.instructions)
        {
            if (auto *p = dynamic_cast<MoveInstruction*>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, nextOffset);
                lowerPseudoSlot(p->dst, pseudoToOffset, nextOffset);
            }else if (auto *p = dynamic_cast<UnaryInstruction*>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, nextOffset);
            }else if (auto *p = dynamic_cast<BinaryInstruction*>(instruction.get()))
            {
                lowerPseudoSlot(p->src, pseudoToOffset, nextOffset);
                lowerPseudoSlot(p->dst, pseudoToOffset, nextOffset);
            }else if (auto *p = dynamic_cast<IDivInstruction*>(instruction.get()))
            {
                lowerPseudoSlot(p->operand, pseudoToOffset, nextOffset);
            }
        }
        if (int frameSize = -nextOffset - 4; frameSize > 0) func.stackAllocation = std::make_unique<AllocateStack>(frameSize);
    }

    void removePseudos(codegenProgram& prog)
    {
        for (auto& node : prog.nodes)
        {
            if (auto *p = dynamic_cast<codegenFunction*>(node.get()))
            {
                removePseudosFromFunction(*p);
            }
        }
    }

    bool isStack(const Operand& op)
    {
        return dynamic_cast<const Stack*>(&op) != nullptr;
    }

    void fixupInstructionsFromFunction(codegenFunction& func)
    {
        std::vector<std::unique_ptr<Instruction>> rewritten;
        rewritten.reserve(func.instructions.size());

        for (auto& instr : func.instructions)
        {
            if (auto* m = dynamic_cast<MoveInstruction*>(instr.get());
                m && isStack(*m->src) && isStack(*m->dst))
            {
                // Mov M1, M2  →  Mov M1, R10 ; Mov R10, M2
                auto r10a = std::make_unique<Register>(RegisterName::R10);
                auto r10b = std::make_unique<Register>(RegisterName::R10);
                rewritten.push_back(std::make_unique<MoveInstruction>(std::move(m->src), std::move(r10a)));
                rewritten.push_back(std::make_unique<MoveInstruction>(std::move(r10b), std::move(m->dst)));
            } else if (auto* m = dynamic_cast<IDivInstruction*>(instr.get()); m && isImmediate(*m->operand))
            {
                std::unique_ptr<Operand> scratchRegister = std::make_unique<Register>(RegisterName::R10);
                m->scratchRegisterInstruction = std::make_unique<MoveInstruction>(std::move(m->operand), std::make_unique<Register>(RegisterName::R10));
                m->operand = std::move(scratchRegister);
                rewritten.push_back(std::move(instr));
            }else if (auto* m = dynamic_cast<BinaryInstruction*>(instr.get()); m
                && (m->op == BinaryOp::Add || m->op == BinaryOp::Subtract || m->op == BinaryOp::BitwiseAnd || m->op == BinaryOp::BitwiseOr || m->op == BinaryOp::BitwiseXor)
                && isStack(*m->src) && isStack(*m->dst))
            {
                // Mov M1, M2  →  Mov M1, R10 ; Mov R10, M2
                auto r10a = std::make_unique<Register>(RegisterName::R10);
                auto r10b = std::make_unique<Register>(RegisterName::R10);
                m->preStackFixInstruction = std::make_unique<MoveInstruction>(std::move(m->src), std::move(r10a));
                m->src = std::move(r10b);
                rewritten.push_back(std::move(instr));
            }else if (auto* m = dynamic_cast<BinaryInstruction*>(instr.get()); m && (m->op == BinaryOp::Multiply) && isStack(*m->dst))
            {
                auto* stack_raw_ptr = dynamic_cast<Stack*>(m->dst.get());
                auto dstcopy1 = std::make_unique<Stack>(*stack_raw_ptr);
                auto dstcopy2 = std::make_unique<Stack>(*stack_raw_ptr);
                auto r11a = std::make_unique<Register>(RegisterName::R11);
                auto r11b = std::make_unique<Register>(RegisterName::R11);
                auto r11c = std::make_unique<Register>(RegisterName::R11);
                m->preStackFixInstruction = std::make_unique<MoveInstruction>(std::move(dstcopy1), std::move(r11a));
                m->dst = std::move(r11b);
                m->postStackFixInstruction = std::make_unique<MoveInstruction>(std::move(r11c), std::move(dstcopy2));
                rewritten.push_back(std::move(instr));
            }else if (auto* m = dynamic_cast<BinaryInstruction*>(instr.get()); m && (m->op == BinaryOp::LeftShift || m->op == BinaryOp::RightShift)
                && !isImmediate(*m->src))
            {
                auto ecxRegister = std::make_unique<Register>(RegisterName::CX);
                auto clRegister  = std::make_unique<Register>(RegisterName::CL);
                m->preStackFixInstruction = std::make_unique<MoveInstruction>(std::move(m->src), std::move(ecxRegister));
                m->src = std::move(clRegister);
                rewritten.push_back(std::move(instr));
            }
            else
            {
                rewritten.push_back(std::move(instr));
            }
        }

        func.instructions = std::move(rewritten);
    }

    void fixupInstructions(codegenProgram& prog)
    {
        for (auto& node : prog.nodes)
        {
            if (auto *p = dynamic_cast<codegenFunction*>(node.get()))
            {
                fixupInstructionsFromFunction(*p);
            }
        }
    }

    std::unique_ptr<codegenProgram> codegen(const TackyProgram& prog)
    {
        std::vector<std::unique_ptr<codegenTopLevelNode>> nodes;
        for (const auto& node : prog.nodes)
        {
            if (auto* p = dynamic_cast<TackyFunction*>(node.get()))
            {
                nodes.push_back(processFunction(*p));
            }
        }

        auto codegenAST = std::make_unique<codegenProgram>(prog.line, prog.column, std::move(nodes));

        removePseudos(*codegenAST);
        fixupInstructions(*codegenAST);
        return codegenAST;
    }
};