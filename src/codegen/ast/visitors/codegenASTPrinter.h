#pragma once
#include "../../../support/RTTI.h"
#include "../../AsmWriter.h"
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
    const AsmWriter &wr;

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

    void visit(const codegenProgram &node) const
    {
        // Each function emits its own .text and each static its own .data/.bss,
        // so sections stay correct regardless of node order.
        for (const auto &child : node.nodes)
        {
            dispatch(*child);
        }
        // Mark the stack non-executable (required for clean SysV/ELF output).
        wr.emitFileEpilogue(out);
    }

    void visit(const codegenStaticVariable &node) const
    {
        if (node.global)
            wr.emitGlobal(out, node.name);

        // All-zero objects (uninitialized statics, fully-padded arrays) go in .bss
        // as a single .zero; anything with a non-zero entry goes in .data.
        const bool allZero =
            std::all_of(node.inits.begin(), node.inits.end(),
                        [](const StaticInit &si) { return si.kind == StaticInit::Kind::Zero; });

        if (allZero)
            wr.emitBssSection(out);
        else
            wr.emitDataSection(out);
        wr.emitAlign(out, node.align);
        out << node.name << ":\n";
        for (const auto &si : node.inits)
        {
            switch (si.kind)
            {
            case StaticInit::Kind::Char:
                wr.emitInitByte(out, si.intVal);
                break;
            case StaticInit::Kind::Int:
                wr.emitInitInt(out, si.intVal);
                break;
            case StaticInit::Kind::Long:
                wr.emitInitLong(out, si.intVal);
                break;
            case StaticInit::Kind::Double:
                // 17 significant digits round-trip an IEEE double exactly through gas.
                wr.emitInitDouble(out, si.dblVal);
                break;
            case StaticInit::Kind::Zero:
                wr.emitInitZero(out, si.zeroBytes);
                break;
            case StaticInit::Kind::String:
                // .asciz appends a null terminator; .ascii does not.
                wr.emitInitString(out, gasStringBody(si.strVal), si.strNull);
                break;
            case StaticInit::Kind::PointerString:
                // Should have been rewritten to PointerLabel in TACKY.
                throw std::runtime_error("unresolved PointerString static initializer");
            case StaticInit::Kind::PointerLabel:
                wr.emitInitPointerLabel(out, si.strVal);
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
        wr.emitRodataSection(out);
        wr.emitAlign(out, node.alignment);
        out << node.name << ":\n";
        wr.emitInitLong(out, static_cast<long long>(bits));
        if (node.alignment == 16)
            wr.emitInitLong(out, 0); // fill the 128-bit slot xorpd reads packed
    }

    void visit(const codegenFunction &node) const
    {
        wr.emitTextSection(out);
        if (node.global)
            wr.emitGlobal(out, node.name);
        out << node.name << ":\n";
        wr.emitFunctionPrologue(out);
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
            out << wr.movsdMnemonic() << "    ";
        else
            out << "mov" << wr.suffix(node.type) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const ReturnInstruction & /*node*/) const { wr.emitReturnSequence(out); }

    void visit(const UnaryOp &op, const char sfx) const
    {
        if (op == UnaryOp::Complement)
        {
            out << "not" << sfx;
        }
        else if (op == UnaryOp::Negate)
        {
            out << "neg" << sfx;
        }
        else if (op == UnaryOp::Shr)
        {
            out << "shr" << sfx;
        }
    }

    // sfx is 'l' for a longword operation, 'q' for a quadword.
    void visit(const BinaryOp &op, const char sfx) const
    {
        if (op == BinaryOp::Add)
        {
            out << "add" << sfx;
        }
        else if (op == BinaryOp::Subtract)
        {
            out << "sub" << sfx;
        }
        else if (op == BinaryOp::Multiply)
        {
            out << "imul" << sfx;
        }
        else if (op == BinaryOp::BitwiseAnd)
        {
            out << "and" << sfx;
        }
        else if (op == BinaryOp::BitwiseOr)
        {
            out << "or" << sfx;
        }
        else if (op == BinaryOp::BitwiseXor)
        {
            out << "xor" << sfx;
        }
        else if (op == BinaryOp::LeftShift)
        {
            out << "shl" << sfx;
        }
        else if (op == BinaryOp::RightShift)
        {
            out << "sar" << sfx;
        }
    }

    void visit(const UnaryInstruction &node) const
    {
        visit(node.op, wr.suffix(node.type));
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
            out << wr.doubleBinaryMnemonic(node.op);
        else
            visit(node.op, wr.suffix(node.type));
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

        out << "idiv" << wr.suffix(node.type) << "    ";
        dispatch(*node.operand);
    }

    void visit(const DivInstruction &node) const
    {
        if (node.scratchRegisterInstruction != nullptr)
        {
            visit(*node.scratchRegisterInstruction);
            out << "\n    ";
        }

        out << "div" << wr.suffix(node.type) << "    ";
        dispatch(*node.operand);
    }

    void visit(const CdqInstruction &node) const { out << wr.cdqMnemonic(node.type); }

    void visit(const CmpInstruction &node) const
    {
        if (node.preStackFixInstruction)
        {
            visit(*node.preStackFixInstruction);
            out << "\n    ";
        }
        if (node.type == AssemblyType::DOUBLE)
            out << wr.ucomisdMnemonic() << "    ";
        else
            out << "cmp" << wr.suffix(node.type) << "    ";
        dispatch(*node.a);
        out << ", ";
        dispatch((*node.b));
    }

    void visit(const JumpInstruction &node) const
    {
        out << "jmp    " << wr.localLabelRef(node.identifier);
    }

    void visit(const JumpCCInstruction &node) const
    {
        out << "j" << condCodeToString(node.condCode) << "    "
            << wr.localLabelRef(node.identifier);
    }

    void visit(const SetCCInstruction &node) const
    {
        out << "set" << condCodeToString(node.condCode) << "    ";
        // setcc writes a single byte: a register operand must use its byte sub-register.
        if (auto *r = dynamic_cast<const Register *>(node.a.get()))
            out << wr.reg(r->name, 1);
        else
            dispatch(*node.a);
    }

    void visit(const Label &node) const { out << wr.localLabelDef(node.identifier); }

    void visit(const MoveSXInstruction &node) const
    {
        // movs<src><dst>: e.g. movsbl (byte->long), movsbq (byte->quad),
        // movslq (long->quad).
        out << "movs" << wr.suffix(node.srcType) << wr.suffix(node.dstType) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const MoveZeroExtendInstruction &node) const
    {
        // Only the byte form survives the fixup pass: movzbl / movzbq.
        out << "movz" << wr.suffix(node.srcType) << wr.suffix(node.dstType) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const PushInstruction &node) const
    {
        out << wr.pushqMnemonic() << "    ";
        dispatch(*node.a);
    }

    void visit(const PopInstruction &node) const
    {
        out << wr.popqMnemonic() << "    ";
        dispatch(*node.reg);
    }

    void visit(const CVTSI2SD &node) const
    {
        // suffix is the integer SOURCE width (l/q); destination is an XMM reg.
        out << wr.cvtsi2sdMnemonic(node.type) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const CVTTSD2SI &node) const
    {
        // suffix is the integer DESTINATION width (l/q); source is an XMM reg/mem.
        out << wr.cvttsd2siMnemonic(node.type) << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void visit(const CallInstruction &node) const
    {
        out << "call    " << wr.callTarget(node.identifier);
    }

    void visit(const LeaInstruction &node) const
    {
        // leaq computes an effective address: always a quadword operation.
        out << wr.leaqMnemonic() << "    ";
        dispatch(*node.src);
        out << ", ";
        dispatch(*node.dst);
    }

    void dispatch(const Instruction &node) const
    {
        switch (node.getKind())
        {
        case InstrKind::MoveInstruction:
        {
            visit(*cast<const MoveInstruction>(&node));
            break;
        }
        case InstrKind::ReturnInstruction:
        {
            visit(*cast<const ReturnInstruction>(&node));
            break;
        }
        case InstrKind::UnaryInstruction:
        {
            visit(*cast<const UnaryInstruction>(&node));
            break;
        }
        case InstrKind::BinaryInstruction:
        {
            visit(*cast<const BinaryInstruction>(&node));
            break;
        }
        case InstrKind::IDivInstruction:
        {
            visit(*cast<const IDivInstruction>(&node));
            break;
        }
        case InstrKind::DivInstruction:
        {
            visit(*cast<const DivInstruction>(&node));
            break;
        }
        case InstrKind::CdqInstruction:
        {
            visit(*cast<const CdqInstruction>(&node));
            break;
        }
        case InstrKind::CmpInstruction:
        {
            visit(*cast<const CmpInstruction>(&node));
            break;
        }
        case InstrKind::JumpInstruction:
        {
            visit(*cast<const JumpInstruction>(&node));
            break;
        }
        case InstrKind::JumpCCInstruction:
        {
            visit(*cast<const JumpCCInstruction>(&node));
            break;
        }
        case InstrKind::SetCCInstruction:
        {
            visit(*cast<const SetCCInstruction>(&node));
            break;
        }
        case InstrKind::Label:
        {
            visit(*cast<const Label>(&node));
            break;
        }
        case InstrKind::PushInstruction:
        {
            visit(*cast<const PushInstruction>(&node));
            break;
        }
        case InstrKind::CallInstruction:
        {
            visit(*cast<const CallInstruction>(&node));
            break;
        }
        case InstrKind::MoveSXInstruction:
        {
            visit(*cast<const MoveSXInstruction>(&node));
            break;
        }
        case InstrKind::MoveZeroExtendInstruction:
        {
            visit(*cast<const MoveZeroExtendInstruction>(&node));
            break;
        }
        case InstrKind::CVTSI2SD:
        {
            visit(*cast<const CVTSI2SD>(&node));
            break;
        }
        case InstrKind::CVTTSD2SI:
        {
            visit(*cast<const CVTTSD2SI>(&node));
            break;
        }
        case InstrKind::LeaInstruction:
        {
            visit(*cast<const LeaInstruction>(&node));
            break;
        }
        case InstrKind::PopInstruction:
        {
            visit(*cast<const PopInstruction>(&node));
            break;
        }
        default:
            throw std::runtime_error("codegenASTPrinter: unhandled instruction kind");
        }
    }

    // disp(%base): the base register is always addressed as 64-bit. A zero
    // displacement is omitted, so Memory(AX, 0) prints as the bare "(%rax)".
    void visit(const Memory &node) const { out << wr.memory(node.reg, node.offset); }

    void visit(const Data &node) const { out << wr.dataRef(node.identifier, node.offset); }

    void visit(const Immediate &node) const { out << wr.immediate(node.value); }

    void visit(const Register &node) const { out << wr.reg(node.name, node.bytes); }

    void dispatch(const Operand &node) const
    {
        switch (node.getKind())
        {
        case OperandKind::Immediate:
        {
            visit(*cast<const Immediate>(&node));
            break;
        }
        case OperandKind::Register:
        {
            visit(*cast<const Register>(&node));
            break;
        }
        case OperandKind::Data:
        {
            visit(*cast<const Data>(&node));
            break;
        }
        case OperandKind::Memory:
        {
            visit(*cast<const Memory>(&node));
            break;
        }
        case OperandKind::PseudoRegister:
        {
            throw std::runtime_error("PseudoRegister leaked past pass 2");
        }
        case OperandKind::PseudoMem:
        {
            throw std::runtime_error("PseudoMem leaked past pass 2");
        }
        default:
            throw std::runtime_error("codegenASTPrinter: unhandled operand kind");
        }
    }

  public:
    codegenASTPrinter(std::ostream &out_, const AsmWriter &writer) : out(out_), wr(writer) {}

    void print(const codegenProgram &node) const { dispatch(node); }
};
