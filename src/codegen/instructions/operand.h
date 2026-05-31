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
    R11
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
    std::string value;
    explicit Immediate(std::string value_) : value(std::move(value_)) {};
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
    explicit PseudoRegister(std::string name_) : name(std::move(name_)) {};
};

class Stack : public Operand
{
  public:
    int depth;
    explicit Stack(const int depth_) : depth(depth_) {};
};