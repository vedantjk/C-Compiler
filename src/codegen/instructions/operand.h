#pragma once
#include <string>

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
    Immediate(std::string value_) : value(value_) {} ;

};

class Register : public Operand
{
    public:
    Register() =default;
};