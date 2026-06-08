#pragma once
#include <string>
#include <utility>

enum class RegisterName
{
    AX,
    DX,
    CX,
    DI,
    SI,
    R8,
    R9,
    R10,
    R11,
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
    Operand() = default;
    virtual ~Operand() = default;
};

class Immediate : public Operand
{
  public:
    long long value;
    explicit Immediate(long long value_) : value(value_) {};
};

class Register : public Operand
{
  public:
    RegisterName name;
    int bytes;
    explicit Register(const RegisterName name_, int bytes_) : name(name_), bytes(bytes_) {};
};

class PseudoRegister : public Operand
{
  public:
    std::string name;
    AssemblyType type; // width of the value, so the stack-slot pass sizes it 4 vs 8
    explicit PseudoRegister(std::string name_, AssemblyType type_ = AssemblyType::LONGWORD)
        : name(std::move(name_)), type(type_) {};
};

class Data : public Operand
{
  public:
    std::string identifier;
    int offset; // byte offset into the object, for member/eightbyte access
    explicit Data(std::string identifier_, int offset_ = 0)
        : identifier(std::move(identifier_)), offset(offset_)
    {
    }
};

class Memory : public Operand
{
  public:
    RegisterName reg;
    int offset;
    Memory(RegisterName reg_, int offset_) : reg(reg_), offset(offset_) {}
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
    PseudoMem(std::string name_, int offset_, AssemblyType type_ = AssemblyType::LONGWORD)
        : name(std::move(name_)), offset(offset_), type(type_)
    {
    }
};