#pragma once
#include "../../instructions/instructions.h"
#include "../ASTNodes/codegenProgram.h"
#include "../TopLevelNodes/codegenFunction.h"
#include "../TopLevelNodes/codegenStaticVariable.h"
#include <memory>
#include <ostream>

class codegenASTPrinter
{
    std::ostream &out;

    void visit(const codegenProgram &node) const
    {
        // Each function emits its own .text and each static its own .data/.bss,
        // so sections stay correct regardless of node order.
        for (const auto &child : node.nodes)
        {
            dispatch(*child);
        }
        // Mark the stack non-executable. Without this section GNU ld defaults to
        // an executable stack and warns; required for clean System V/ELF output.
        out << "    .section .note.GNU-stack,\"\",@progbits\n";
    }

    void visit(const codegenStaticVariable &node) const
    {
        const bool isLong = node.type == ConstantType::LONG;
        const int align = isLong ? 8 : 4;
        if (node.global)
            out << "    .globl " << node.name << "\n";
        if (node.init != 0)
        {
            out << "    .data\n";
            out << "    .align " << align << "\n";
            out << node.name << ":\n";
            out << (isLong ? "    .quad " : "    .long ") << node.init << "\n";
        }
        else
        {
            out << "    .bss\n";
            out << "    .align " << align << "\n";
            out << node.name << ":\n";
            out << (isLong ? "    .zero 8\n" : "    .zero 4\n");
        }
    }

    void visit(const codegenFunction &node) const
    {
        out << "    .text\n";
        if (node.global)
            out << "    .globl " << node.name << "\n";
        out << node.name << ":\n";
        out << "    pushq    %rbp\n";
        out << "    movq    %rsp, %rbp\n";
        if (node.stackAllocation != nullptr)
        {
            out << "    ";
            visit(*node.stackAllocation);
            out << "\n";
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
        else if (auto *p = dynamic_cast<const codegenStaticVariable *>(&node))
        {
            visit(*p);
        }
    }

    void visit(const MoveInstruction &node) const
    {
        out << "mov" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
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

    void visit(const UnaryOp &op, const char suffix) const
    {
        if (op == UnaryOp::Complement)
        {
            out << "not" << suffix;
        }
        else if (op == UnaryOp::Negate)
        {
            out << "neg" << suffix;
        }
    }

    // suffix is 'l' for a longword operation, 'q' for a quadword.
    void visit(const BinaryOp &op, const char suffix) const
    {
        if (op == BinaryOp::Add)
        {
            out << "add" << suffix;
        }
        else if (op == BinaryOp::Subtract)
        {
            out << "sub" << suffix;
        }
        else if (op == BinaryOp::Multiply)
        {
            out << "imul" << suffix;
        }
        else if (op == BinaryOp::BitwiseAnd)
        {
            out << "and" << suffix;
        }
        else if (op == BinaryOp::BitwiseOr)
        {
            out << "or" << suffix;
        }
        else if (op == BinaryOp::BitwiseXor)
        {
            out << "xor" << suffix;
        }
        else if (op == BinaryOp::LeftShift)
        {
            out << "shl" << suffix;
        }
        else if (op == BinaryOp::RightShift)
        {
            out << "sar" << suffix;
        }
    }

    void visit(const UnaryInstruction &node) const
    {
        visit(node.op, node.type == AssemblyType::QUADWORD ? 'q' : 'l');
        out << "    ";
        dispatch(*node.operand);
    }

    void visit(const BinaryInstruction &node) const
    {
        if (node.preStackFixInstruction != nullptr)
        {
            visit(*node.preStackFixInstruction);
            out << "\n    ";
        }
        visit(node.op, node.type == AssemblyType::QUADWORD ? 'q' : 'l');
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

        out << "idiv" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
        dispatch(*node.operand);
    }

    void visit(const CdqInstruction &node) const
    {
        out << (node.type == AssemblyType::QUADWORD ? "cqo" : "cdq");
    }

    void visit(const CmpInstruction &node) const
    {
        if (node.preStackFixInstruction)
        {
            visit(*node.preStackFixInstruction);
            out << "\n    ";
        }
        out << "cmp" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
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

    void visit(const MoveSXInstruction &node) const
    {
        out << "movslq    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const PushInstruction &node) const
    {
        out << "pushq    ";
        dispatch(*node.a);
    }

    void visit(const CallInstruction &node) const
    {
        out << "call    " << node.identifier << "@PLT";
    }

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
        else if (auto *p = dynamic_cast<const PushInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const CallInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const MoveSXInstruction *>(&node))
        {
            visit(*p);
        }
    }

    void visit(const Stack &node) const { out << node.depth << "(%rbp)"; }

    void visit(const Data &node) const { out << node.identifier << "(%rip)"; }

    void visit(const Immediate &node) const { out << "$" << node.value; }

    void visit(const Register &node) const
    {
        if (node.name == RegisterName::AX)
        {
            if (node.bytes == 8)
                out << "%rax";
            else if (node.bytes == 4)
                out << "%eax";
            else
                out << "%al";
        }
        else if (node.name == RegisterName::DX)
        {
            if (node.bytes == 8)
                out << "%rdx";
            else if (node.bytes == 4)
                out << "%edx";
            else
                out << "%dl";
        }
        else if (node.name == RegisterName::CX)
        {
            if (node.bytes == 8)
                out << "%rcx";
            else if (node.bytes == 4)
                out << "%ecx";
            else
                out << "%cl";
        }
        else if (node.name == RegisterName::DI)
        {
            if (node.bytes == 8)
                out << "%rdi";
            else if (node.bytes == 4)
                out << "%edi";
            else
                out << "%dil";
        }
        else if (node.name == RegisterName::SI)
        {
            if (node.bytes == 8)
                out << "%rsi";
            else if (node.bytes == 4)
                out << "%esi";
            else
                out << "%sil";
        }
        else if (node.name == RegisterName::R8)
        {
            if (node.bytes == 8)
                out << "%r8";
            else if (node.bytes == 4)
                out << "%r8d";
            else
                out << "%r8b";
        }
        else if (node.name == RegisterName::R9)
        {
            if (node.bytes == 8)
                out << "%r9";
            else if (node.bytes == 4)
                out << "%r9d";
            else
                out << "%r9b";
        }
        else if (node.name == RegisterName::R10)
        {
            if (node.bytes == 8)
                out << "%r10";
            else if (node.bytes == 4)
                out << "%r10d";
            else
                out << "%r10b";
        }
        else if (node.name == RegisterName::R11)
        {
            if (node.bytes == 8)
                out << "%r11";
            else if (node.bytes == 4)
                out << "%r11d";
            else
                out << "%r11b";
        }
        else if (node.name == RegisterName::SP)
        {
            out << "%rsp"; // the stack pointer is always addressed as the 64-bit register
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
        else if (auto *p = dynamic_cast<const Data *>(&node))
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
