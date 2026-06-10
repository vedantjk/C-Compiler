#pragma once
#include <string>
#include <utility>

enum class OperandKind
{
    Immediate,
    Register,
    Data,
    Memory,
    PseudoRegister,
    PseudoMem,
};

enum class RegisterName
{
    AX,
    BX,
    DX,
    CX,
    DI,
    SI,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
    SP,
    BP,
    XMM0,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15
};

inline bool isXmm(RegisterName r) { return r >= RegisterName::XMM0; }

enum class CondCode
{
    E,
    NE,
    G,
    GE,
    L,
    LE,
    A,
    AE,
    B,
    BE
};

enum class AssemblyType
{
    BYTE,
    LONGWORD,
    QUADWORD,
    DOUBLE
};

inline std::string condCodeToString(const CondCode c)
{
    if (c == CondCode::E)
        return "E";
    if (c == CondCode::NE)
        return "NE";
    if (c == CondCode::G)
        return "G";
    if (c == CondCode::GE)
        return "GE";
    if (c == CondCode::L)
        return "L";
    if (c == CondCode::LE)
        return "LE";
    if (c == CondCode::A)
        return "A";
    if (c == CondCode::AE)
        return "AE";
    if (c == CondCode::B)
        return "B";
    if (c == CondCode::BE)
        return "BE";
    return "";
}

class Operand
{
  public:
    const OperandKind kind;
    explicit Operand(OperandKind k) : kind(k) {}
    virtual ~Operand() = default;
    OperandKind getKind() const { return kind; }
};

class Immediate : public Operand
{
  public:
    long long value;
    explicit Immediate(long long value_) : Operand(OperandKind::Immediate), value(value_) {}
    static bool classof(OperandKind k) { return k == OperandKind::Immediate; }
};

class Register : public Operand
{
  public:
    RegisterName name;
    int bytes;
    explicit Register(const RegisterName name_, int bytes_)
        : Operand(OperandKind::Register), name(name_), bytes(bytes_)
    {
    }
    static bool classof(OperandKind k) { return k == OperandKind::Register; }
};

class PseudoRegister : public Operand
{
  public:
    std::string name;
    AssemblyType type; // width of the value, so the stack-slot pass sizes it 4 vs 8

    // Self-typing fields copied from TackyVar (inert until CG2 reads them in
    // lowerPseudoSlot; side-maps remain authoritative this stage).
    bool isStatic = false;
    long long objSize = 0;
    int objAlign = 0;
    std::string structTag;

    explicit PseudoRegister(std::string name_, AssemblyType type_ = AssemblyType::LONGWORD)
        : Operand(OperandKind::PseudoRegister), name(std::move(name_)), type(type_)
    {
    }
    static bool classof(OperandKind k) { return k == OperandKind::PseudoRegister; }
};

class Data : public Operand
{
  public:
    std::string identifier;
    int offset; // byte offset into the object, for member/eightbyte access
    explicit Data(std::string identifier_, int offset_ = 0)
        : Operand(OperandKind::Data), identifier(std::move(identifier_)), offset(offset_)
    {
    }
    static bool classof(OperandKind k) { return k == OperandKind::Data; }
};

class Memory : public Operand
{
  public:
    RegisterName reg;
    int offset;
    Memory(RegisterName reg_, int offset_)
        : Operand(OperandKind::Memory), reg(reg_), offset(offset_)
    {
    }
    static bool classof(OperandKind k) { return k == OperandKind::Memory; }
};

// A reference into an aggregate object's storage at a byte offset, before the
// stack-slot pass resolves it. Lowered to Memory(BP, slotBase + offset) for a
// local object. `type` sizes the access (mov vs movsd width).
class PseudoMem : public Operand
{
  public:
    std::string name;
    int offset;
    AssemblyType type;

    // Self-typing fields copied from TackyVar (inert until CG2 reads them in
    // lowerPseudoSlot; side-maps remain authoritative this stage).
    bool isStatic = false;
    long long objSize = 0;
    int objAlign = 0;
    std::string structTag;

    PseudoMem(std::string name_, int offset_, AssemblyType type_ = AssemblyType::LONGWORD)
        : Operand(OperandKind::PseudoMem), name(std::move(name_)), offset(offset_), type(type_)
    {
    }
    static bool classof(OperandKind k) { return k == OperandKind::PseudoMem; }
};