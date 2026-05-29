#pragma once
#include "../../instructions/instructions.h"
#include "../ASTNodes/codegenProgram.h"
#include "../TopLevelNodes/codegenFunction.h"
#include <memory>
#include <ostream>

class codegenASTPrinter
{
    std::ostream &out;

    void visit(const codegenProgram &node) const
    {
        out << "    .text\n";
        for (const auto &child : node.nodes)
        {
            dispatch(*child);
        }
        // Mark the stack non-executable. Without this section GNU ld defaults to
        // an executable stack and warns; required for clean System V/ELF output.
        out << "    .section .note.GNU-stack,\"\",@progbits\n";
    }

    void visit(const codegenFunction &node) const
    {
        out << "    .globl " << node.name << "\n";
        out << node.name << ":\n";
        out << "    pushq    %rbp\n";
        out << "    movq    %rsp, %rbp\n";
        if (node.stackAllocation != nullptr)
        {
            visit(*node.stackAllocation);
        }
        for (const auto &child : node.instructions)
        {
            if (auto *p = dynamic_cast<Label *>(child.get()); p == nullptr)
                out << "    ";
            dispatch(*child);
            out << std::endl;
        }
    }

    void dispatch(const codegenASTNode &node) const
    {
        if (auto *p = dynamic_cast<const codegenProgram *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const codegenFunction *>(&node))
        {
            visit(*p);
        }
    }

    void visit(const MoveInstruction &node) const
    {
        out << "movl    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const ReturnInstruction &node) const
    {
        out << "movq    %rbp, %rsp\n";
        out << "    popq    %rbp\n";
        out << "    ret";
    }

    void visit(const UnaryOp &op) const
    {
        if (op == UnaryOp::Complement)
        {
            out << "notl";
        }
        else if (op == UnaryOp::Negate)
        {
            out << "negl";
        }
    }

    void visit(const BinaryOp &op) const
    {
        if (op == BinaryOp::Add)
        {
            out << "addl";
        }
        else if (op == BinaryOp::Subtract)
        {
            out << "subl";
        }
        else if (op == BinaryOp::Multiply)
        {
            out << "imull";
        }
        else if (op == BinaryOp::BitwiseAnd)
        {
            out << "andl";
        }
        else if (op == BinaryOp::BitwiseOr)
        {
            out << "orl";
        }
        else if (op == BinaryOp::BitwiseXor)
        {
            out << "xorl";
        }
        else if (op == BinaryOp::LeftShift)
        {
            out << "shll";
        }
        else if (op == BinaryOp::RightShift)
        {
            out << "sarl";
        }
    }

    void visit(const UnaryInstruction &node) const
    {
        visit(node.op);
        out << "    ";
        dispatch(*node.operand);
    }

    void visit(const AllocateStack &node) const
    {
        out << "    subq    $" << node.quantity << ", %rsp\n";
    }

    void visit(const BinaryInstruction &node) const
    {
        if (node.preStackFixInstruction != nullptr)
        {
            visit(*node.preStackFixInstruction);
            out << "\n    ";
        }
        visit(node.op);
        out << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
        if (node.postStackFixInstruction != nullptr)
        {
            out << "\n    ";
            visit(*node.postStackFixInstruction);
        }
    }

    void visit(const IDivInstruction &node) const
    {
        if (node.scratchRegisterInstruction != nullptr)
        {
            visit(*node.scratchRegisterInstruction);
            out << "\n    ";
        }

        out << "idivl    ";
        dispatch(*node.operand);
    }

    void visit(const CdqInstruction &node) const { out << "cdq"; }

    void visit(const CmpInstruction &node) const
    {
        if (node.preStackFixInstruction)
        {
            visit(*node.preStackFixInstruction);
            out << "\n    ";
        }
        out << "cmpl    ";
        dispatch(*node.a);
        out << ", ";
        dispatch((*node.b));
    }

    void visit(const JumpInstruction &node) const { out << "jmp    .L" << node.identifier; }

    void visit(const JumpCCInstruction &node) const
    {
        out << "j" << condCodeToString(node.condCode) << "    .L" << node.identifier;
    }

    void visit(const SetCCInstruction &node) const
    {
        out << "set" << condCodeToString(node.condCode) << "    ";
        dispatch(*node.a);
    }

    void visit(const Label &node) const { out << ".L" << node.identifier << ":"; }

    void dispatch(const Instruction &node) const
    {
        if (auto *p = dynamic_cast<const MoveInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const ReturnInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const UnaryInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const BinaryInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const IDivInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const CdqInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const CmpInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const JumpInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const JumpCCInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const SetCCInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const Label *>(&node))
        {
            visit(*p);
        }
    }

    void visit(const Stack &node) const { out << node.depth << "(%rbp)"; }

    void visit(const Immediate &node) const { out << "$" << node.value; }

    void visit(const Register &node) const
    {
        if (node.name == RegisterName::AX)
        {
            out << "%eax";
        }
        else if (node.name == RegisterName::R10)
        {
            out << "%r10d";
        }
        else if (node.name == RegisterName::DX)
        {
            out << "%edx";
        }
        else if (node.name == RegisterName::R11)
        {
            out << "%r11d";
        }
        else if (node.name == RegisterName::CL)
        {
            out << "%cl";
        }
        else if (node.name == RegisterName::CX)
        {
            out << "%ecx";
        }
    }

    void dispatch(const Operand &node) const
    {
        if (auto *p = dynamic_cast<const Immediate *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const Register *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const Stack *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const PseudoRegister *>(&node))
        {
            throw std::runtime_error("PseudoRegister leaked past pass 2");
        }
    }

  public:
    codegenASTPrinter(std::ostream &out_) : out(out_) {}

    void print(const codegenProgram &node) const { dispatch(node); }
};
