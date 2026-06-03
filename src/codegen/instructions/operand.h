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
    SP
};

enum class CondCode
{
    E,
    NE,
    G,
    GE,
    L,
    LE
};

enum class AssemblyType
{
    LONGWORD,
    QUADWORD
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

class Stack : public Operand
{
  public:
    int depth;
    explicit Stack(const int depth_) : depth(depth_) {};
};

class Data : public Operand
{
  public:
    std::string identifier;
    explicit Data(std::string identifier_) : identifier(std::move(identifier_)) {}
};