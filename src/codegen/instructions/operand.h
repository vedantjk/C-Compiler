#pragma once
#include <string>
#include <utility>

enum class RegisterName
{
    AX,
    R10,
    DX,
    R11,
    CL,
    CX
};

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
      explicit Immediate(std::string value_) : value(std::move(value_)) {} ;

};

class Register : public Operand
{
    public:
    RegisterName name;
      explicit Register(const RegisterName name) : name(name) {} ;
};

class PseudoRegister : public Operand
{
    public:
    std::string name;
      explicit PseudoRegister(std::string name_) : name(std::move(name_)) {} ;
};

class Stack : public Operand
{
    public:
    int depth;
      explicit Stack(const int depth_) : depth(depth_) {} ;
};