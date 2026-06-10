#pragma once
#include "../utils/common.h"
#include "instructions/operand.h"
#include <iomanip>
#include <ostream>
#include <string>

// ---------------------------------------------------------------------------
// AsmWriter — abstract interface for assembly SYNTAX decisions.
//
// The printer (codegenASTPrinter) owns the structural walk: which operands
// each instruction has and in what order they appear.  Every concrete token
// (register names with their % sigil, size suffixes b/l/q, $N immediates,
// (%rip) and @PLT decorators, section directives, frame sequences) is
// delegated here.  Adding a new target (e.g. Intel/MASM syntax) requires
// only a new subclass — no changes to the printer.
// ---------------------------------------------------------------------------

struct AsmWriter
{
    virtual ~AsmWriter() = default;

    // ---- operand tokens ----------------------------------------------------

    // "%rax", "%eax", "%al", "%xmm0", … (includes the % sigil)
    virtual std::string reg(RegisterName name, int bytes) const = 0;

    // Size suffix character for integer operations: 'b' / 'l' / 'q'.
    // DOUBLE has no integer-size suffix and returns '\0'.
    virtual char suffix(AssemblyType t) const = 0;

    // Immediate operand: "$N"
    virtual std::string immediate(long long value) const = 0;

    // Displacement + base-register memory: "off(%rbase)" or "(%rbase)"
    // The base register is always addressed as 64-bit.
    virtual std::string memory(RegisterName base, int offset) const = 0;

    // RIP-relative data reference: "name(%rip)" or "name+off(%rip)"
    virtual std::string dataRef(const std::string &name, int offset) const = 0;

    // Call target with the appropriate PLT/GOT decoration: "name@PLT"
    virtual std::string callTarget(const std::string &name) const = 0;

    // Local label reference: ".Lid"
    virtual std::string localLabelRef(const std::string &id) const = 0;

    // Local label definition: ".Lid:"
    virtual std::string localLabelDef(const std::string &id) const = 0;

    // ---- instruction tokens ------------------------------------------------

    // cdq / cqo based on type (LONGWORD → cdq, QUADWORD → cqo)
    virtual std::string cdqMnemonic(AssemblyType t) const = 0;

    // movsd (scalar double move)
    virtual std::string movsdMnemonic() const = 0;

    // ucomisd (scalar double compare — unordered, sets ZF/CF)
    virtual std::string ucomisdMnemonic() const = 0;

    // Scalar double arithmetic mnemonic: addsd / subsd / mulsd / divsd / xorpd
    virtual std::string doubleBinaryMnemonic(BinaryOp op) const = 0;

    // cvtsi2sd{l|q}: integer-to-double; suffix is the SOURCE integer width
    virtual std::string cvtsi2sdMnemonic(AssemblyType srcType) const = 0;

    // cvttsd2si{l|q}: double-to-integer (truncating); suffix is DEST integer width
    virtual std::string cvttsd2siMnemonic(AssemblyType dstType) const = 0;

    // leaq — load effective address, always quadword
    virtual std::string leaqMnemonic() const = 0;

    // pushq — push, always quadword
    virtual std::string pushqMnemonic() const = 0;

    // popq — pop, always quadword
    virtual std::string popqMnemonic() const = 0;

    // ---- frame sequences ---------------------------------------------------

    // Emit the function prologue lines (after the label):
    //   pushq %rbp
    //   movq  %rsp, %rbp
    virtual void emitFunctionPrologue(std::ostream &out) const = 0;

    // Emit the return sequence (restores frame + ret), without trailing newline:
    //   movq %rbp, %rsp
    //   popq %rbp
    //   ret
    virtual void emitReturnSequence(std::ostream &out) const = 0;

    // ---- section + data directives -----------------------------------------

    virtual void emitGlobal(std::ostream &out, const std::string &name) const = 0;
    virtual void emitTextSection(std::ostream &out) const = 0;
    virtual void emitDataSection(std::ostream &out) const = 0;
    virtual void emitBssSection(std::ostream &out) const = 0;
    virtual void emitRodataSection(std::ostream &out) const = 0;
    virtual void emitAlign(std::ostream &out, int align) const = 0;

    // Static initializer directives
    virtual void emitInitByte(std::ostream &out, long long val) const = 0;
    virtual void emitInitInt(std::ostream &out, long long val) const = 0;
    virtual void emitInitLong(std::ostream &out, long long val) const = 0;
    virtual void emitInitDouble(std::ostream &out, double val) const = 0;
    virtual void emitInitZero(std::ostream &out, int bytes) const = 0;
    virtual void emitInitString(std::ostream &out, const std::string &body,
                                bool nullTerminated) const = 0;
    virtual void emitInitPointerLabel(std::ostream &out, const std::string &label) const = 0;

    // End-of-file marker (e.g. .note.GNU-stack on ELF targets)
    virtual void emitFileEpilogue(std::ostream &out) const = 0;
};

// ---------------------------------------------------------------------------
// SysVAsmWriter — AT&T syntax, System V / ELF x86-64 ABI.
// This is the only concrete implementation today; a second one can be added
// without touching the printer.
// ---------------------------------------------------------------------------

struct SysVAsmWriter : AsmWriter
{
    // ---- operand tokens ----------------------------------------------------

    std::string reg(RegisterName name, int bytes) const override
    {
        switch (name)
        {
        case RegisterName::AX:
            if (bytes == 8)
                return "%rax";
            if (bytes == 4)
                return "%eax";
            return "%al";
        case RegisterName::DX:
            if (bytes == 8)
                return "%rdx";
            if (bytes == 4)
                return "%edx";
            return "%dl";
        case RegisterName::CX:
            if (bytes == 8)
                return "%rcx";
            if (bytes == 4)
                return "%ecx";
            return "%cl";
        case RegisterName::DI:
            if (bytes == 8)
                return "%rdi";
            if (bytes == 4)
                return "%edi";
            return "%dil";
        case RegisterName::SI:
            if (bytes == 8)
                return "%rsi";
            if (bytes == 4)
                return "%esi";
            return "%sil";
        case RegisterName::R8:
            if (bytes == 8)
                return "%r8";
            if (bytes == 4)
                return "%r8d";
            return "%r8b";
        case RegisterName::R9:
            if (bytes == 8)
                return "%r9";
            if (bytes == 4)
                return "%r9d";
            return "%r9b";
        case RegisterName::R10:
            if (bytes == 8)
                return "%r10";
            if (bytes == 4)
                return "%r10d";
            return "%r10b";
        case RegisterName::R11:
            if (bytes == 8)
                return "%r11";
            if (bytes == 4)
                return "%r11d";
            return "%r11b";
        case RegisterName::BX:
            if (bytes == 8)
                return "%rbx";
            if (bytes == 4)
                return "%ebx";
            return "%bl";
        case RegisterName::R12:
            if (bytes == 8)
                return "%r12";
            if (bytes == 4)
                return "%r12d";
            return "%r12b";
        case RegisterName::R13:
            if (bytes == 8)
                return "%r13";
            if (bytes == 4)
                return "%r13d";
            return "%r13b";
        case RegisterName::R14:
            if (bytes == 8)
                return "%r14";
            if (bytes == 4)
                return "%r14d";
            return "%r14b";
        case RegisterName::R15:
            if (bytes == 8)
                return "%r15";
            if (bytes == 4)
                return "%r15d";
            return "%r15b";
        // SP and BP are always 64-bit
        case RegisterName::SP:
            return "%rsp";
        case RegisterName::BP:
            return "%rbp";
        // XMM registers: bytes is ignored (no sub-register forms in SSE)
        case RegisterName::XMM0:
            return "%xmm0";
        case RegisterName::XMM1:
            return "%xmm1";
        case RegisterName::XMM2:
            return "%xmm2";
        case RegisterName::XMM3:
            return "%xmm3";
        case RegisterName::XMM4:
            return "%xmm4";
        case RegisterName::XMM5:
            return "%xmm5";
        case RegisterName::XMM6:
            return "%xmm6";
        case RegisterName::XMM7:
            return "%xmm7";
        case RegisterName::XMM8:
            return "%xmm8";
        case RegisterName::XMM9:
            return "%xmm9";
        case RegisterName::XMM10:
            return "%xmm10";
        case RegisterName::XMM11:
            return "%xmm11";
        case RegisterName::XMM12:
            return "%xmm12";
        case RegisterName::XMM13:
            return "%xmm13";
        case RegisterName::XMM14:
            return "%xmm14";
        case RegisterName::XMM15:
            return "%xmm15";
        default:
            return "%??";
        }
    }

    char suffix(AssemblyType t) const override
    {
        if (t == AssemblyType::BYTE)
            return 'b';
        if (t == AssemblyType::QUADWORD)
            return 'q';
        if (t == AssemblyType::LONGWORD)
            return 'l';
        return '\0'; // DOUBLE: no integer size suffix
    }

    std::string immediate(long long value) const override { return "$" + std::to_string(value); }

    std::string memory(RegisterName base, int offset) const override
    {
        // Zero displacement is omitted: Memory(BP, 0) → "(%rbp)"
        if (offset != 0)
            return std::to_string(offset) + "(" + reg(base, 8) + ")";
        return "(" + reg(base, 8) + ")";
    }

    std::string dataRef(const std::string &name, int offset) const override
    {
        if (offset != 0)
            return name + "+" + std::to_string(offset) + "(%rip)";
        return name + "(%rip)";
    }

    std::string callTarget(const std::string &name) const override { return name + "@PLT"; }

    std::string localLabelRef(const std::string &id) const override { return ".L" + id; }

    std::string localLabelDef(const std::string &id) const override { return ".L" + id + ":"; }

    // ---- instruction tokens ------------------------------------------------

    std::string cdqMnemonic(AssemblyType t) const override
    {
        return t == AssemblyType::QUADWORD ? "cqo" : "cdq";
    }

    std::string movsdMnemonic() const override { return "movsd"; }

    std::string ucomisdMnemonic() const override { return "ucomisd"; }

    std::string doubleBinaryMnemonic(BinaryOp op) const override
    {
        switch (op)
        {
        case BinaryOp::Add:
            return "addsd";
        case BinaryOp::Subtract:
            return "subsd";
        case BinaryOp::Multiply:
            return "mulsd";
        case BinaryOp::Divide:
            return "divsd";
        case BinaryOp::Xor:
            return "xorpd";
        default:
            return "???sd";
        }
    }

    std::string cvtsi2sdMnemonic(AssemblyType srcType) const override
    {
        return std::string("cvtsi2sd") + suffix(srcType);
    }

    std::string cvttsd2siMnemonic(AssemblyType dstType) const override
    {
        return std::string("cvttsd2si") + suffix(dstType);
    }

    std::string leaqMnemonic() const override { return "leaq"; }

    std::string pushqMnemonic() const override { return "pushq"; }

    std::string popqMnemonic() const override { return "popq"; }

    // ---- frame sequences ---------------------------------------------------

    void emitFunctionPrologue(std::ostream &out) const override
    {
        out << "    pushq    %rbp\n";
        out << "    movq    %rsp, %rbp\n";
    }

    void emitReturnSequence(std::ostream &out) const override
    {
        out << "movq    %rbp, %rsp\n";
        out << "    popq    %rbp\n";
        out << "    ret";
    }

    // ---- section + data directives -----------------------------------------

    void emitGlobal(std::ostream &out, const std::string &name) const override
    {
        out << "    .globl " << name << "\n";
    }

    void emitTextSection(std::ostream &out) const override { out << "    .text\n"; }

    void emitDataSection(std::ostream &out) const override { out << "    .data\n"; }

    void emitBssSection(std::ostream &out) const override { out << "    .bss\n"; }

    void emitRodataSection(std::ostream &out) const override { out << "    .section .rodata\n"; }

    void emitAlign(std::ostream &out, int align) const override
    {
        out << "    .align " << align << "\n";
    }

    void emitInitByte(std::ostream &out, long long val) const override
    {
        out << "    .byte " << val << "\n";
    }

    void emitInitInt(std::ostream &out, long long val) const override
    {
        out << "    .long " << val << "\n";
    }

    void emitInitLong(std::ostream &out, long long val) const override
    {
        out << "    .quad " << val << "\n";
    }

    void emitInitDouble(std::ostream &out, double val) const override
    {
        out << "    .double " << std::setprecision(17) << val << "\n";
    }

    void emitInitZero(std::ostream &out, int bytes) const override
    {
        out << "    .zero " << bytes << "\n";
    }

    void emitInitString(std::ostream &out, const std::string &body,
                        bool nullTerminated) const override
    {
        out << (nullTerminated ? "    .asciz \"" : "    .ascii \"") << body << "\"\n";
    }

    void emitInitPointerLabel(std::ostream &out, const std::string &label) const override
    {
        out << "    .quad " << label << "\n";
    }

    void emitFileEpilogue(std::ostream &out) const override
    {
        // Mark the stack non-executable (required for clean SysV/ELF output).
        out << "    .section .note.GNU-stack,\"\",@progbits\n";
    }
};
