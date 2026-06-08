#pragma once
#include "../../instructions/instructions.h"
#include "../ASTNodes/codegenProgram.h"
#include "../TopLevelNodes/codegenFunction.h"
#include "../TopLevelNodes/codegenStaticConstant.h"
#include "../TopLevelNodes/codegenStaticVariable.h"
#include <algorithm>
#include <bit>
#include <cstdio>
#include <iomanip>
#include <memory>
#include <ostream>
#include <variant>

class codegenASTPrinter
{
    std::ostream &out;

    // Render raw bytes as a gas string-literal body: printable characters as-is
    // (with " and \ escaped), everything else as a 3-digit octal escape.
    static std::string gasStringBody(const std::string &bytes)
    {
        std::string out;
        for (unsigned char b : bytes)
        {
            if (b == '"' || b == '\\')
            {
                out += '\\';
                out += static_cast<char>(b);
            }
            else if (b >= 32 && b < 127)
            {
                out += static_cast<char>(b);
            }
            else
            {
                char buf[5];
                std::snprintf(buf, sizeof(buf), "\\%03o", b);
                out += buf;
            }
        }
        return out;
    }

    // AT&T operation-size suffix for an integer assembly type.
    static char suffix(const AssemblyType t)
    {
        if (t == AssemblyType::BYTE)
            return 'b';
        if (t == AssemblyType::QUADWORD)
            return 'q';
        return 'l';
    }

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
        if (node.global)
            out << "    .globl " << node.name << "\n";

        // All-zero objects (uninitialized statics, fully-padded arrays) go in .bss
        // as a single .zero; anything with a non-zero entry goes in .data.
        const bool allZero =
            std::all_of(node.inits.begin(), node.inits.end(),
                        [](const StaticInit &si) { return si.kind == StaticInit::Kind::Zero; });

        out << (allZero ? "    .bss\n" : "    .data\n");
        out << "    .align " << node.align << "\n";
        out << node.name << ":\n";
        for (const auto &si : node.inits)
        {
            switch (si.kind)
            {
            case StaticInit::Kind::Char:
                out << "    .byte " << si.intVal << "\n";
                break;
            case StaticInit::Kind::Int:
                out << "    .long " << si.intVal << "\n";
                break;
            case StaticInit::Kind::Long:
                out << "    .quad " << si.intVal << "\n";
                break;
            case StaticInit::Kind::Double:
                // 17 significant digits round-trip an IEEE double exactly through gas.
                out << "    .double " << std::setprecision(17) << si.dblVal << "\n";
                break;
            case StaticInit::Kind::Zero:
                out << "    .zero " << si.zeroBytes << "\n";
                break;
            case StaticInit::Kind::String:
                // .asciz appends a null terminator; .ascii does not.
                out << (si.strNull ? "    .asciz \"" : "    .ascii \"") << gasStringBody(si.strVal)
                    << "\"\n";
                break;
            }
        }
    }

    void visit(const codegenStaticConstant &node) const
    {
        // Read-only double constants (literals, the -0.0 negation mask, the 2^63
        // conversion bias). Emitted as the raw bit pattern via .quad so values like
        // -0.0 are exact rather than relying on gas re-parsing a printed double.
        const auto bits = std::bit_cast<unsigned long long>(node.value);
        out << "    .section .rodata\n";
        out << "    .align " << node.alignment << "\n";
        out << node.name << ":\n";
        out << "    .quad " << bits << "\n";
        if (node.alignment == 16)
            out << "    .quad 0\n"; // fill the 128-bit slot xorpd reads packed
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
        else if (auto *p = dynamic_cast<const codegenStaticConstant *>(&node))
        {
            visit(*p);
        }
    }

    void visit(const MoveInstruction &node) const
    {
        if (node.type == AssemblyType::DOUBLE)
            out << "movsd    ";
        else
            out << "mov" << suffix(node.type) << "    ";
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
        else if (op == UnaryOp::Shr)
        {
            out << "shr" << suffix;
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

    // SSE scalar-double arithmetic: distinct mnemonics, no l/q suffix.
    void visitDoubleBinaryOp(const BinaryOp &op) const
    {
        if (op == BinaryOp::Add)
            out << "addsd";
        else if (op == BinaryOp::Subtract)
            out << "subsd";
        else if (op == BinaryOp::Multiply)
            out << "mulsd";
        else if (op == BinaryOp::Divide)
            out << "divsd";
        else if (op == BinaryOp::Xor)
            out << "xorpd";
    }

    void visit(const UnaryInstruction &node) const
    {
        visit(node.op, suffix(node.type));
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
        if (node.type == AssemblyType::DOUBLE)
            visitDoubleBinaryOp(node.op);
        else
            visit(node.op, suffix(node.type));
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

    void visit(const DivInstruction &node) const
    {
        if (node.scratchRegisterInstruction != nullptr)
        {
            visit(*node.scratchRegisterInstruction);
            out << "\n    ";
        }

        out << "div" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
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
        if (node.type == AssemblyType::DOUBLE)
            out << "ucomisd    ";
        else
            out << "cmp" << suffix(node.type) << "    ";
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
        // movs<src><dst>: e.g. movsbl (byte->long), movsbq (byte->quad),
        // movslq (long->quad).
        out << "movs" << suffix(node.srcType) << suffix(node.dstType) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const MoveZeroExtendInstruction &node) const
    {
        // Only the byte form survives the fixup pass: movzbl / movzbq.
        out << "movz" << suffix(node.srcType) << suffix(node.dstType) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const PushInstruction &node) const
    {
        out << "pushq    ";
        dispatch(*node.a);
    }

    void visit(const CVTSI2SD &node) const
    {
        // suffix is the integer SOURCE width (l/q); destination is an XMM reg.
        out << "cvtsi2sd" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const CVTTSD2SI &node) const
    {
        // suffix is the integer DESTINATION width (l/q); source is an XMM reg/mem.
        out << "cvttsd2si" << (node.type == AssemblyType::QUADWORD ? 'q' : 'l') << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const CallInstruction &node) const
    {
        out << "call    " << node.identifier << "@PLT";
    }

    void visit(const LeaInstruction &node) const
    {
        // leaq computes an effective address: always a quadword operation.
        out << "leaq    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
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
        else if (auto *p = dynamic_cast<const DivInstruction *>(&node))
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
        else if (auto *p = dynamic_cast<const MoveZeroExtendInstruction *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const CVTSI2SD *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const CVTTSD2SI *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const LeaInstruction *>(&node))
        {
            visit(*p);
        }
        else
        {
            throw std::runtime_error("codegenASTPrinter: unhandled instruction kind");
        }
    }

    // disp(%base): the base register is always addressed as 64-bit. A zero
    // displacement is omitted, so Memory(AX, 0) prints as the bare "(%rax)".
    void visit(const Memory &node) const
    {
        if (node.offset != 0)
            out << node.offset;
        out << "(";
        visit(Register(node.reg, 8));
        out << ")";
    }

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
        else if (node.name == RegisterName::BP)
        {
            out << "%rbp"; // the frame pointer is always addressed as the 64-bit register
        }
        // XMM registers have no sub-register widths: `bytes` is ignored.
        else if (node.name == RegisterName::XMM0)
            out << "%xmm0";
        else if (node.name == RegisterName::XMM1)
            out << "%xmm1";
        else if (node.name == RegisterName::XMM2)
            out << "%xmm2";
        else if (node.name == RegisterName::XMM3)
            out << "%xmm3";
        else if (node.name == RegisterName::XMM4)
            out << "%xmm4";
        else if (node.name == RegisterName::XMM5)
            out << "%xmm5";
        else if (node.name == RegisterName::XMM6)
            out << "%xmm6";
        else if (node.name == RegisterName::XMM7)
            out << "%xmm7";
        else if (node.name == RegisterName::XMM14)
            out << "%xmm14";
        else if (node.name == RegisterName::XMM15)
            out << "%xmm15";
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
        else if (auto *p = dynamic_cast<const Data *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const Memory *>(&node))
        {
            visit(*p);
        }
        else if (auto *p = dynamic_cast<const PseudoRegister *>(&node))
        {
            throw std::runtime_error("PseudoRegister leaked past pass 2");
        }
        else
        {
            throw std::runtime_error("codegenASTPrinter: unhandled operand kind");
        }
    }

  public:
    codegenASTPrinter(std::ostream &out_) : out(out_) {}

    void print(const codegenProgram &node) const { dispatch(node); }
};
